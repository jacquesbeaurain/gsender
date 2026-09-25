#include "step_through_model.hpp"

#include "backend.hpp"
#include "gcode_highlighter.hpp"
#include "machine.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QLocale>
#include <QPainter>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gs::ui {
namespace {

constexpr std::size_t kMaxMatches = 5000;

QString localeNumber(double value) {
    // toLocaleString(): grouped, up to 3 decimals.
    QString text = QLocale().toString(value, 'f', 3);
    const QChar point = QLocale().decimalPoint().front();
    if (text.contains(point)) {
        while (text.endsWith('0')) {
            text.chop(1);
        }
        if (text.endsWith(point)) {
            text.chop(1);
        }
    }
    return text;
}

bool containsIgnoringCase(const std::string& line, const std::string& lowerNeedle) {
    const auto it = std::search(line.begin(), line.end(), lowerNeedle.begin(), lowerNeedle.end(),
                                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
    return it != line.end();
}

}  // namespace

// ---- the source rows ------------------------------------------------------------------

void StepSourceModel::setLines(const std::vector<std::string>* lines, bool dark) {
    beginResetModel();
    lines_ = lines;
    dark_ = dark;
    matched_.assign(lines ? lines->size() : 0, false);
    current_ = 0;
    endResetModel();
}

void StepSourceModel::setCurrent(std::size_t line) {
    const std::size_t previous = current_;
    current_ = line;
    changed(previous);
    changed(line);
}

void StepSourceModel::setMatches(const std::vector<std::size_t>& matches) {
    std::fill(matched_.begin(), matched_.end(), false);
    for (const std::size_t line : matches) {
        matched_[line - 1] = true;
    }
    if (rowCount() > 0) {
        Q_EMIT dataChanged(index(0), index(rowCount() - 1), {MatchRole});
    }
}

int StepSourceModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || !lines_ ? 0 : static_cast<int>(lines_->size());
}

QVariant StepSourceModel::data(const QModelIndex& index, int role) const {
    if (!lines_ || !index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const std::size_t line = static_cast<std::size_t>(index.row()) + 1;
    switch (role) {
        case NumberRole: return static_cast<int>(line);
        // Coloured as the row is shown (upstream colours only the rows on screen).
        case CodeRole: return app::gcodeStyledText(QString::fromStdString((*lines_)[line - 1]), dark_);
        case CurrentRole: return line == current_;
        case MatchRole: return static_cast<bool>(matched_[line - 1]);
        default: return {};
    }
}

QHash<int, QByteArray> StepSourceModel::roleNames() const {
    return {{NumberRole, "number"}, {CodeRole, "code"}, {CurrentRole, "current"}, {MatchRole, "matched"}};
}

void StepSourceModel::changed(std::size_t line) {
    if (line >= 1 && lines_ && line <= lines_->size()) {
        const QModelIndex at = index(static_cast<int>(line) - 1);
        Q_EMIT dataChanged(at, at, {CurrentRole});
    }
}

// ---- the model ------------------------------------------------------------------------

StepThroughModel::StepThroughModel(QObject* parent)
    : QObject(parent), machine_(UiBackend::instance()->machine()) {
    playTimer_ = new QTimer(this);
    playTimer_->setInterval(16);  // requestAnimationFrame
    connect(playTimer_, &QTimer::timeout, this, &StepThroughModel::tick);
}

StepThroughModel::~StepThroughModel() {
    if (cancel_) {
        *cancel_ = true;
    }
}

void StepThroughModel::load() {
    stopPlaying();
    text_ = machine_.programText();
    lines_.clear();
    for (const std::string_view line : str::splitLines(text_)) {
        lines_.emplace_back(line);
    }
    source_.setLines(&lines_, machine_.settings().darkMode);
    matches_.clear();
    searching_ = false;
    line_ = 1;
    const job::ProgramAnalysis& analysis = machine_.analysis();
    tools_ = machine_.isAnalyzing() ? std::vector<job::StepperTool>{}
                                    : job::stepperTools(analysis.spindleToolEvents, lines_.size(), analysis.tools,
                                                        app::mainViewTheme(machine_).cutting.name().toStdString());
    hiddenTools_.assign(tools_.size(), false);
    buildIndex();
    source_.setCurrent(line_);
    Q_EMIT loaded();
    Q_EMIT toolsChanged();
    Q_EMIT matchesChanged();
    Q_EMIT lineChanged();
}

void StepThroughModel::buildIndex() {
    if (cancel_) {
        *cancel_ = true;
    }
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    indexReady_ = false;
    indexProgress_ = 0;
    index_ = {};
    updateSpans();
    Q_EMIT indexChanged();
    const std::uint64_t generation = ++generation_;
    auto text = std::make_shared<const std::string>(text_);
    auto cancel = cancel_;
    QPointer<StepThroughModel> guard(this);
    QThreadPool::globalInstance()->start([guard, text, cancel, generation] {
        auto index = std::make_shared<job::StepIndex>(job::buildStepIndex(
            *text, {}, [cancel] { return cancel->load(); },
            [guard, generation](std::size_t done, std::size_t total) {
                const int percent = total > 0 ? static_cast<int>(100 * done / total) : 100;
                QMetaObject::invokeMethod(qApp, [guard, generation, percent] {
                    if (guard && guard->generation_ == generation && !guard->indexReady_) {
                        guard->indexProgress_ = percent;
                        Q_EMIT guard->indexChanged();
                    }
                });
            }));
        if (cancel->load()) {
            return;
        }
        QMetaObject::invokeMethod(qApp, [guard, generation, index] {
            if (guard && guard->generation_ == generation) {
                guard->index_ = std::move(*index);
                guard->indexReady_ = true;
                guard->indexProgress_ = 100;
                guard->updateSpans();
                Q_EMIT guard->indexChanged();
                Q_EMIT guard->lineChanged();
            }
        });
    });
}

bool StepThroughModel::waitForIndex(int timeoutMs) {
    QDeadlineTimer deadline(timeoutMs);
    while (!indexReady_ && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return indexReady_;
}

void StepThroughModel::updateSpans() {
    // Each tool's lines as sender lines: from those run before its first
    // line to those run through its last (buildToolFrameGroups()).
    spans_.clear();
    if (indexReady_) {
        for (std::size_t i = 0; i < tools_.size(); ++i) {
            const job::StepperTool& tool = tools_[i];
            spans_.push_back({index_.streamedThrough(tool.startLine - 1), index_.streamedThrough(tool.endLine),
                              QColor(QString::fromStdString(tool.color)), hiddenTools_[i]});
        }
    }
}

void StepThroughModel::goToLine(int line) {
    stopPlaying();
    setLine(static_cast<double>(std::max(1, line)));
}

void StepThroughModel::setLine(double line) {
    const std::size_t total = lines_.size();
    if (total == 0) {
        return;
    }
    line_ = static_cast<std::size_t>(std::clamp(std::round(line), 1.0, static_cast<double>(total)));
    source_.setCurrent(line_);
    Q_EMIT lineChanged();
}

void StepThroughModel::step(int delta) {
    goToLine(static_cast<int>(std::max<long long>(1, static_cast<long long>(line_) + delta)));
}

void StepThroughModel::reset() {
    stopPlaying();
    setLine(1);
}

void StepThroughModel::togglePlay() {
    const std::size_t total = lines_.size();
    if (total == 0) {
        return;
    }
    if (playing_) {
        stopPlaying();
        return;
    }
    if (line_ >= total) {
        setLine(1);  // from the start again
    }
    playing_ = true;
    playStartLine_ = static_cast<double>(line_);
    playClock_.start();
    playTimer_->start();
    Q_EMIT lineChanged();
}

void StepThroughModel::stopPlaying() {
    if (!playing_) {
        return;
    }
    playing_ = false;
    playTimer_->stop();
    Q_EMIT lineChanged();
}

void StepThroughModel::setSpeed(double speed) {
    if (speed <= 0 || speed == speed_) {
        return;
    }
    speed_ = speed;
    if (playing_) {
        // The pace changes from here.
        playStartLine_ = static_cast<double>(line_);
        playClock_.start();
    }
    Q_EMIT lineChanged();
}

void StepThroughModel::tick() {
    const std::size_t total = lines_.size();
    const double perSecond = job::playbackLinesPerSecond(total, machine_.analysis().estimatedTime, speed_);
    const double next = playStartLine_ + static_cast<double>(playClock_.elapsed()) / 1000.0 * perSecond;
    if (next >= static_cast<double>(total)) {
        setLine(static_cast<double>(total));
        stopPlaying();
        return;
    }
    setLine(next);
}

void StepThroughModel::setHideProcessed(bool hide) {
    if (hide != hideProcessed_) {
        hideProcessed_ = hide;
        Q_EMIT lineChanged();
    }
}

int StepThroughModel::activeTool() const {
    return job::activeToolIndex(tools_, line_);
}

void StepThroughModel::toggleTool(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= hiddenTools_.size()) {
        return;
    }
    hiddenTools_[static_cast<std::size_t>(index)] = !hiddenTools_[static_cast<std::size_t>(index)];
    updateSpans();
    Q_EMIT toolsChanged();
    Q_EMIT lineChanged();  // the view
}

QVariantList StepThroughModel::tools() const {
    const bool metric = machine_.settings().metric;
    QVariantList list;
    for (std::size_t i = 0; i < tools_.size(); ++i) {
        const job::StepperTool& tool = tools_[i];
        QStringList details;
        if (tool.diameter) {
            details << QString::fromUtf8("⌀ ") +
                           (metric ? QString::fromStdString(js::toFixed(*tool.diameter, 3)) + " mm"
                                   : QString::fromStdString(js::toFixed(*tool.diameter / 25.4, 4)) + " in");
        }
        if (tool.spindleSpeed) {
            details << tr("%1 RPM").arg(localeNumber(*tool.spindleSpeed));
        }
        list.append(QVariantMap{
            {"index", tool.index},
            {"number", QString::fromStdString(js::numberToString(tool.toolNumber))},
            {"comment", QString::fromStdString(tool.comment)},
            {"color", QString::fromStdString(tool.color)},
            {"textColor", QString::fromStdString(job::readableTextColor(tool.color))},
            {"details", details.join("   ")},
            {"range", QString("%1-%2").arg(tool.startLine).arg(tool.endLine)},
            {"startLine", static_cast<int>(tool.startLine)},
            {"hidden", static_cast<bool>(hiddenTools_[i])},
        });
    }
    return list;
}

void StepThroughModel::setSearch(const QString& query) {
    matches_.clear();
    const std::string needle = query.trimmed().toLower().toStdString();
    searching_ = !needle.empty();
    if (searching_) {
        for (std::size_t i = 0; i < lines_.size() && matches_.size() < kMaxMatches; ++i) {
            if (containsIgnoringCase(lines_[i], needle)) {
                matches_.push_back(i + 1);
            }
        }
    }
    source_.setMatches(matches_);
    Q_EMIT matchesChanged();
}

void StepThroughModel::goToNextMatch() {
    if (matches_.empty()) {
        return;
    }
    const auto next = std::upper_bound(matches_.begin(), matches_.end(), line_);
    goToLine(static_cast<int>(next != matches_.end() ? *next : matches_.front()));
}

QStringList StepThroughModel::quarterLabels() const {
    QStringList labels;
    for (int i = 0; i < 5; ++i) {
        const double quarter = std::round(i * 0.25 * static_cast<double>(lines_.size()));
        labels << QLocale().toString(static_cast<qlonglong>(std::max(1.0, quarter)));
    }
    return labels;
}

QString StepThroughModel::positionTitle() const {
    return machine_.settings().metric ? tr("Position (Work, mm)") : tr("Position (Work, in)");
}

gcode::Vec4 StepThroughModel::position() const {
    return indexReady_ ? index_.position(line_) : gcode::Vec4{};
}

std::size_t StepThroughModel::done() const {
    return indexReady_ ? index_.streamedThrough(line_) : 0;
}

QString StepThroughModel::positionText() const {
    // Always mm in the index; both units show 2 decimals.
    const bool metric = machine_.settings().metric;
    const gcode::Vec4 p = position();
    const auto linear = [metric](double mm) {
        return QString::fromStdString(js::toFixed(metric ? mm : mm / 25.4, 2));
    };
    QString text = QString("X %1   Y %2   Z %3").arg(linear(p.x), linear(p.y), linear(p.z));
    if (machine_.analysis().usedAxes.find('A') != std::string::npos) {
        text += QString::fromUtf8("   A %1°").arg(QString::fromStdString(js::toFixed(p.a, 2)));
    }
    return text;
}

QVariantList StepThroughModel::modals() const {
    const job::StepModalReadout current = indexReady_ ? index_.modals(line_) : job::StepModalReadout{};
    const job::StepModalReadout previous =
        indexReady_ && line_ > 1 ? index_.modals(line_ - 1) : job::StepModalReadout{};
    QVariantList list;
    for (std::size_t i = 0; i < current.size(); ++i) {
        list.append(QVariantMap{
            {"label", tr(job::kStepModalLabels[i]).toUpper()},
            {"value", current[i].empty() ? QString::fromUtf8("—") : QString::fromStdString(current[i])},
            {"changed", indexReady_ && line_ > 1 && previous[i] != current[i]},
        });
    }
    return list;
}

QString StepThroughModel::lineText() const {
    return line_ >= 1 && line_ <= lines_.size() ? QString::fromStdString(lines_[line_ - 1]) : QString();
}

// ---- the view -------------------------------------------------------------------------

void StepToolpathItem::setModel(StepThroughModel* model) {
    if (model == model_) {
        return;
    }
    if (model_) {
        disconnect(model_, nullptr, this, nullptr);
    }
    model_ = model;
    if (model_) {
        connect(model_, &StepThroughModel::lineChanged, this, [this] { update(); });
        connect(model_, &StepThroughModel::indexChanged, this, [this] { update(); });
        connect(model_, &StepThroughModel::loaded, this, &StepToolpathItem::fit);
    }
    Q_EMIT modelChanged();
    update();
}

bool StepToolpathItem::rotaryJob() const {
    const app::Machine* m = machine();
    return m && m->hasProgram() && !m->isAnalyzing() && m->analysis().fileType != gcode::FileType::Default;
}

std::optional<gcode::BoundingBox> StepToolpathItem::contentBounds() const {
    const app::Machine* m = machine();
    if (!m) {
        return std::nullopt;
    }
    const app::Toolpath& path = m->toolpath();
    if (!m->hasProgram() || m->isAnalyzing() || (path.feeds.empty() && path.rapids.empty())) {
        return std::nullopt;
    }
    return rotaryJob() && path.bounded ? path.bounds : m->analysis().bounds;
}

void StepToolpathItem::paintContent(QPainter& painter, app::ToolpathCamera& camera) {
    const app::Machine* m = machine();
    const app::VisualizerTheme& colors = app::mainViewTheme(*m);
    const std::optional<gcode::BoundingBox> bounds = contentBounds();
    app::scene::paintBackground(painter, camera, colors, bounds);
    const gcode::Vec4 position = model_ ? model_->position() : gcode::Vec4{};
    if (bounds && model_) {
        const app::Toolpath& path = m->toolpath();
        const std::size_t done = model_->done();
        const bool hideProcessed = model_->hideProcessed();
        const auto& spans = model_->spans();
        std::vector<QPen> spanPens;
        for (const StepThroughModel::Span& span : spans) {
            spanPens.emplace_back(span.color, 1.5);
        }
        const auto spanFor = [&spans](std::uint32_t line) -> const StepThroughModel::Span* {
            // Spans follow each other in line order.
            const auto after = std::upper_bound(spans.begin(), spans.end(), line,
                                                [](std::uint32_t l, const auto& span) { return l < span.first; });
            if (after == spans.begin()) {
                return nullptr;
            }
            const auto& span = *(after - 1);
            return line < span.end ? &span : nullptr;
        };
        QColor rapidColor = colors.rapid;
        rapidColor.setAlphaF(0.3f);
        const QPen rapid(rapidColor, 1, Qt::DashLine);
        const QPen rapidDone(colors.processed, 1, Qt::DashLine);
        const QPen cut(colors.cutting, 1.5);
        const QPen cutDone(colors.processed, 1.5);
        const auto pens = [&](bool feed) {
            return [&, feed](std::size_t, std::uint32_t line) -> const QPen* {
                const StepThroughModel::Span* span = spanFor(line);
                if (span && span->hidden) {
                    return nullptr;
                }
                if (line < done) {
                    return hideProcessed ? nullptr : (feed ? &cutDone : &rapidDone);
                }
                if (!feed) {
                    return &rapid;
                }
                return span ? &spanPens[static_cast<std::size_t>(span - spans.data())] : &cut;
            };
        };
        // A rotary job turns with the line's A (setToolpathRotationA).
        const double rotation = rotaryJob() ? position.a : 0;
        app::scene::paintSegments(painter, camera, path.rapids, path.rapidLines, pens(false), rotation);
        app::scene::paintSegments(painter, camera, path.feeds, path.feedLines, pens(true), rotation);
    }
    app::scene::paintTool(painter, camera, colors, {position.x, position.y, position.z});
}

}  // namespace gs::ui
