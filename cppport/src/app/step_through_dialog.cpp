#include "step_through_dialog.hpp"

#include "machine.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <QAbstractListModel>
#include <QApplication>
#include <QButtonGroup>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QPainter>
#include <QPointer>
#include <QProxyStyle>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gs::app {
namespace {

constexpr std::size_t kMaxMatches = 5000;
constexpr int kSteps[4] = {-1000, -100, 100, 1000};
constexpr double kSpeeds[4] = {0.5, 1, 10, 100};

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

QString sectionTitle(const QString& text) {
    return QString("<span style='font-size:8pt;letter-spacing:1px;color:#8b95a1'>%1</span>")
        .arg(text.toUpper().toHtmlEscaped());
}

// A click anywhere on the scrubber's track jumps there.
class JumpSliderStyle final : public QProxyStyle {
public:
    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override {
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons) {
            return Qt::LeftButton;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

bool containsIgnoringCase(const std::string& line, const std::string& lowerNeedle) {
    const auto it = std::search(line.begin(), line.end(), lowerNeedle.begin(), lowerNeedle.end(),
                                [](char a, char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) == b;
                                });
    return it != line.end();
}

}  // namespace

// ---- the source list ----------------------------------------------------------------------

// The file's lines with their numbers: the current one marked and bold, the
// search's matches highlighted.
class StepSourceModel final : public QAbstractListModel {
public:
    explicit StepSourceModel(QObject* parent) : QAbstractListModel(parent) {
        bold_ = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        bold_.setBold(true);
    }

    void setLines(const std::vector<std::string>* lines) {
        beginResetModel();
        lines_ = lines;
        gutter_ = static_cast<int>(QString::number(lines ? lines->size() : 0).size());
        matched_.assign(lines ? lines->size() : 0, false);
        current_ = 0;
        endResetModel();
    }

    void setCurrent(std::size_t line) {
        const std::size_t previous = current_;
        current_ = line;
        changed(previous);
        changed(line);
    }

    void setMatches(const std::vector<std::size_t>& matches) {
        std::fill(matched_.begin(), matched_.end(), false);
        for (const std::size_t line : matches) {
            matched_[line - 1] = true;
        }
        if (rowCount({}) > 0) {
            Q_EMIT dataChanged(index(0), index(rowCount({}) - 1), {Qt::BackgroundRole});
        }
    }

    int rowCount(const QModelIndex& parent) const override {
        return parent.isValid() || !lines_ ? 0 : static_cast<int>(lines_->size());
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!lines_ || !index.isValid()) {
            return {};
        }
        const std::size_t line = static_cast<std::size_t>(index.row()) + 1;
        const bool current = line == current_;
        switch (role) {
            case Qt::DisplayRole:
                return QString("%1 %2  %3")
                    .arg(current ? QChar(0x203A) : QChar(' '))
                    .arg(static_cast<qulonglong>(line), gutter_)
                    .arg(QString::fromStdString((*lines_)[line - 1]));
            case Qt::BackgroundRole:
                if (current) {
                    return QColor(0x3b, 0x82, 0xf6, 70);
                }
                if (matched_[line - 1]) {
                    return QColor(0xfa, 0xcc, 0x15, 80);
                }
                return {};
            case Qt::FontRole:
                return current ? QVariant(bold_) : QVariant();
            default:
                return {};
        }
    }

private:
    void changed(std::size_t line) {
        if (line >= 1 && lines_ && line <= lines_->size()) {
            const QModelIndex at = index(static_cast<int>(line) - 1);
            Q_EMIT dataChanged(at, at);
        }
    }

    const std::vector<std::string>* lines_ = nullptr;
    std::vector<bool> matched_;
    std::size_t current_ = 0;
    int gutter_ = 1;
    QFont bold_;
};

// ---- the view -----------------------------------------------------------------------------

StepThroughView::StepThroughView(Machine& machine, QWidget* parent) : ToolpathCanvas(parent), machine_(machine) {
    setMinimumSize(320, 240);
}

std::optional<gcode::BoundingBox> StepThroughView::contentBounds() const {
    const Toolpath& path = machine_.toolpath();
    const bool empty = !machine_.hasProgram() || machine_.isAnalyzing() || (path.feeds.empty() && path.rapids.empty());
    if (empty) {
        return std::nullopt;
    }
    return rotaryJob() && path.bounded ? path.bounds : machine_.analysis().bounds;
}

bool StepThroughView::rotaryJob() const {
    return machine_.hasProgram() && !machine_.isAnalyzing() &&
           machine_.analysis().fileType != gcode::FileType::Default;
}

void StepThroughView::seekTo(const gcode::Vec4& position, std::size_t done, bool hideProcessed) {
    position_ = position;
    done_ = done;
    hideProcessed_ = hideProcessed;
    update();
}

void StepThroughView::setTools(std::vector<ToolSpan> spans) {
    spans_ = std::move(spans);
    spanPens_.clear();
    for (const ToolSpan& span : spans_) {
        spanPens_.emplace_back(span.color, 1.5);
    }
    update();
}

void StepThroughView::setOverlay(const QString& text) {
    overlay_ = text;
    update();
}

const StepThroughView::ToolSpan* StepThroughView::spanFor(std::uint32_t line) const {
    // Spans follow each other in line order.
    const auto after = std::upper_bound(spans_.begin(), spans_.end(), line,
                                        [](std::uint32_t l, const ToolSpan& span) { return l < span.first; });
    if (after == spans_.begin()) {
        return nullptr;
    }
    const ToolSpan& span = *(after - 1);
    return line < span.end ? &span : nullptr;
}

void StepThroughView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const std::optional<gcode::BoundingBox> bounds = contentBounds();
    paintScene(painter, bounds);
    if (bounds) {
        const Toolpath& path = machine_.toolpath();
        const QPen rapid(kRapid, 1, Qt::DashLine);
        const QPen rapidDone(kDone, 1, Qt::DashLine);
        const QPen cut(kCut, 1.5);
        const QPen cutDone(kDone, 1.5);
        const auto pens = [&](bool feed) {
            return [&, feed](std::size_t, std::uint32_t line) -> const QPen* {
                const ToolSpan* span = spanFor(line);
                if (span && span->hidden) {
                    return nullptr;
                }
                if (line < done_) {
                    return hideProcessed_ ? nullptr : (feed ? &cutDone : &rapidDone);
                }
                if (!feed) {
                    return &rapid;
                }
                return span ? &spanPens_[static_cast<std::size_t>(span - spans_.data())] : &cut;
            };
        };
        // A rotary job turns with the line's A (setToolpathRotationA).
        const double rotation = rotaryJob() ? position_.a : 0;
        paintSegments(painter, path.rapids, path.rapidLines, pens(false), rotation);
        paintSegments(painter, path.feeds, path.feedLines, pens(true), rotation);
    }
    paintTool(painter, {position_.x, position_.y, position_.z});
    if (!overlay_.isEmpty()) {
        paintCaption(painter, overlay_);
    }
}

// ---- the dialog ---------------------------------------------------------------------------

StepThroughDialog::StepThroughDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("G-code Step Through"));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    resize(parent ? parent->window()->size() * 0.92 : QSize(1280, 820));
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    auto* layout = new QVBoxLayout(this);

    auto* columns = new QHBoxLayout;
    layout->addLayout(columns, 1);

    // Source: the lines, searchable, the current one followed.
    auto* sourceColumn = new QVBoxLayout;
    auto* sourceHeader = new QHBoxLayout;
    sourceTitle_ = new QLabel;
    searchToggle_ = new QToolButton;
    searchToggle_->setText(tr("Search"));
    searchToggle_->setCheckable(true);
    searchToggle_->setToolTip(tr("Search G-code"));
    sourceHeader->addWidget(sourceTitle_);
    sourceHeader->addStretch(1);
    sourceHeader->addWidget(searchToggle_);
    sourceColumn->addLayout(sourceHeader);
    searchRow_ = new QWidget;
    auto* searchLayout = new QHBoxLayout(searchRow_);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search..."));
    matchCount_ = new QLabel(QString::fromUtf8("—"));
    searchLayout->addWidget(search_, 1);
    searchLayout->addWidget(matchCount_);
    searchRow_->hide();
    sourceColumn->addWidget(searchRow_);
    source_ = new QListView;
    source_->setFont(mono);
    source_->setUniformItemSizes(true);
    source_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    source_->setSelectionMode(QAbstractItemView::NoSelection);
    source_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    model_ = new StepSourceModel(this);
    source_->setModel(model_);
    sourceColumn->addWidget(source_, 1);
    auto* toStart = new QPushButton(tr("Go to start"));
    sourceColumn->addWidget(toStart);
    columns->addLayout(sourceColumn, 1);

    // The toolpath, and the whole of the current line under it.
    auto* centre = new QVBoxLayout;
    view_ = new StepThroughView(machine_);
    centre->addWidget(view_, 1);
    lineReadout_ = new QFrame;
    lineReadout_->setFrameShape(QFrame::StyledPanel);
    auto* readoutLayout = new QHBoxLayout(lineReadout_);
    lineNumber_ = new QLabel;
    lineNumber_->setFont(mono);
    lineNumber_->setAlignment(Qt::AlignRight | Qt::AlignTop);
    lineNumber_->setStyleSheet("color:palette(mid)");
    lineText_ = new QLabel;
    lineText_->setFont(mono);
    lineText_->setWordWrap(true);
    lineText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lineText_->setTextFormat(Qt::PlainText);
    readoutLayout->addWidget(lineNumber_);
    readoutLayout->addWidget(lineText_, 1);
    centre->addWidget(lineReadout_);
    columns->addLayout(centre, 3);

    // Tools: one card each, the active one outlined.
    auto* toolsColumn = new QVBoxLayout;
    toolsColumn->addWidget(new QLabel(sectionTitle(tr("Tools"))));
    auto* toolsScroll = new QScrollArea;
    toolsScroll->setWidgetResizable(true);
    toolsScroll->setFrameShape(QFrame::NoFrame);
    toolsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* toolsHolder = new QWidget;
    toolList_ = new QVBoxLayout(toolsHolder);
    toolList_->setContentsMargins(0, 0, 4, 0);
    toolsEmpty_ = new QLabel(tr("No tools found in this file."));
    static_cast<QLabel*>(toolsEmpty_)->setAlignment(Qt::AlignCenter);
    toolsEmpty_->setStyleSheet("color:palette(mid)");
    toolList_->addWidget(toolsEmpty_);
    toolList_->addStretch(1);
    toolsScroll->setWidget(toolsHolder);
    toolsColumn->addWidget(toolsScroll, 1);
    auto* toolsWidget = new QWidget;
    toolsWidget->setLayout(toolsColumn);
    toolsWidget->setMinimumWidth(220);
    columns->addWidget(toolsWidget, 1);

    // The scrubber over the whole file.
    scrubber_ = new QSlider(Qt::Horizontal);
    auto* jump = new JumpSliderStyle;
    jump->setParent(scrubber_);
    scrubber_->setStyle(jump);
    scrubber_->setPageStep(100);
    scrubber_->setToolTip(tr("G-code line"));
    layout->addWidget(scrubber_);
    auto* quarters = new QGridLayout;
    for (int i = 0; i < 5; ++i) {
        quarterLabels_[i] = new QLabel;
        quarterLabels_[i]->setStyleSheet("color:palette(mid)");
        quarterLabels_[i]->setAlignment(i == 0 ? Qt::AlignLeft : i == 4 ? Qt::AlignRight : Qt::AlignHCenter);
        quarters->addWidget(quarterLabels_[i], 0, i);
        quarters->setColumnStretch(i, 1);
    }
    layout->addLayout(quarters);
    position_ = new QLabel;
    position_->setAlignment(Qt::AlignCenter);
    layout->addWidget(position_);

    // Step controls: play/pause, reset, the speed, and the coarse steps
    // (single lines are a click in the source).
    layout->addWidget(new QLabel(sectionTitle(tr("Step Controls"))));
    auto* playRow = new QHBoxLayout;
    play_ = new QPushButton(tr("Play"));
    play_->setCheckable(true);
    play_->setMinimumHeight(40);
    reset_ = new QPushButton(tr("Reset"));
    reset_->setToolTip(tr("Reset to start"));
    reset_->setMinimumHeight(40);
    playRow->addWidget(play_);
    playRow->addWidget(reset_);
    speeds_ = new QButtonGroup(this);
    for (int i = 0; i < 4; ++i) {
        auto* speed = new QPushButton(js::numberToString(kSpeeds[i]).append("x").c_str());
        speed->setCheckable(true);
        speed->setMinimumHeight(40);
        speed->setChecked(kSpeeds[i] == speed_);
        speeds_->addButton(speed, i);
        playRow->addWidget(speed, 1);
    }
    layout->addLayout(playRow);
    auto* stepRow = new QHBoxLayout;
    for (int i = 0; i < 4; ++i) {
        const int delta = kSteps[i];
        steps_[i] = new QPushButton(delta < 0 ? QString::fromUtf8("« -%1 lines").arg(-delta)
                                              : QString::fromUtf8("+%1 lines »").arg(delta));
        steps_[i]->setMinimumHeight(40);
        steps_[i]->setToolTip(delta < 0 ? tr("Back %1 lines").arg(-delta) : tr("Forward %1 lines").arg(delta));
        connect(steps_[i], &QPushButton::clicked, this, [this, delta] { step(delta); });
        stepRow->addWidget(steps_[i]);
    }
    layout->addLayout(stepRow);

    // Status: the work position, the modal state (what this line changed
    // lit up) and the processed-lines switch.
    auto* status = new QHBoxLayout;
    auto* positionBox = new QFrame;
    positionBox->setFrameShape(QFrame::StyledPanel);
    auto* positionLayout = new QVBoxLayout(positionBox);
    positionTitle_ = new QLabel;
    positionValues_ = new QLabel;
    positionValues_->setFont(mono);
    positionLayout->addWidget(positionTitle_);
    positionLayout->addWidget(positionValues_, 1);
    status->addWidget(positionBox);
    auto* modalBox = new QFrame;
    modalBox->setFrameShape(QFrame::StyledPanel);
    auto* modalLayout = new QVBoxLayout(modalBox);
    modalLayout->addWidget(new QLabel(sectionTitle(tr("Modals"))));
    auto* cells = new QGridLayout;
    cells->setSpacing(4);
    for (int i = 0; i < 11; ++i) {
        modalCells_[i] = new QLabel;
        modalCells_[i]->setTextFormat(Qt::RichText);
        modalCells_[i]->setAlignment(Qt::AlignCenter);
        modalCells_[i]->setMinimumWidth(64);
        cells->addWidget(modalCells_[i], i / 6, i % 6);
    }
    modalLayout->addLayout(cells);
    status->addWidget(modalBox, 1);
    hideProcessedButton_ = new QPushButton;
    hideProcessedButton_->setCheckable(true);
    hideProcessedButton_->setMinimumWidth(140);
    status->addWidget(hideProcessedButton_);
    layout->addLayout(status);

    playTimer_ = new QTimer(this);
    playTimer_->setInterval(16);
    connect(playTimer_, &QTimer::timeout, this, &StepThroughDialog::tick);
    connect(play_, &QPushButton::clicked, this, [this] { togglePlay(); });
    connect(reset_, &QPushButton::clicked, this, &StepThroughDialog::reset);
    connect(speeds_, &QButtonGroup::idClicked, this, [this](int id) { setPlaybackSpeed(kSpeeds[id]); });
    connect(toStart, &QPushButton::clicked, this, [this] { goToLine(1); });
    connect(source_, &QListView::clicked, this,
            [this](const QModelIndex& index) { goToLine(static_cast<std::size_t>(index.row()) + 1); });
    connect(searchToggle_, &QToolButton::toggled, this, [this](bool open) {
        searchRow_->setVisible(open);
        search_->clear();
        if (open) {
            search_->setFocus();
        }
    });
    connect(search_, &QLineEdit::textChanged, this, &StepThroughDialog::setSearch);
    connect(search_, &QLineEdit::returnPressed, this, &StepThroughDialog::goToNextMatch);
    connect(scrubber_, &QSlider::sliderPressed, this, [this] {
        stopPlaying();
        scrubbing_ = true;
    });
    connect(scrubber_, &QSlider::valueChanged, this, [this](int value) {
        if (scrubber_->isSliderDown()) {
            setLine(value);
        } else {
            goToLine(static_cast<std::size_t>(value));
        }
    });
    connect(scrubber_, &QSlider::sliderReleased, this, [this] {
        scrubbing_ = false;
        goToLine(static_cast<std::size_t>(scrubber_->value()));
        refresh();
    });
    connect(hideProcessedButton_, &QPushButton::clicked, this, [this](bool on) { setHideProcessed(on); });
    // The file changing under the dialog: gone closes it, another one starts
    // over, the analysis finishing brings the tools and the path.
    connect(&machine_, &Machine::programChanged, this, [this] {
        if (!machine_.hasProgram()) {
            close();
        } else if (machine_.programText() != text_) {
            load();
        } else if (!machine_.isAnalyzing()) {
            loadTools();
            view_->fit();
            refresh();
        }
    });
    setHideProcessed(false);
    load();
}

StepThroughDialog::~StepThroughDialog() {
    if (cancel_) {
        *cancel_ = true;
    }
}

void StepThroughDialog::load() {
    stopPlaying();
    text_ = machine_.programText();
    lines_.clear();
    for (const std::string_view line : str::splitLines(text_)) {
        lines_.emplace_back(line);
    }
    model_->setLines(&lines_);
    matches_.clear();
    search_->clear();
    const std::size_t total = lines_.size();
    sourceTitle_->setText(sectionTitle(tr("G-code (%1 lines)").arg(QLocale().toString(static_cast<qlonglong>(total)))));
    {
        const QSignalBlocker block(scrubber_);
        scrubber_->setRange(1, static_cast<int>(std::max<std::size_t>(total, 1)));
        scrubber_->setValue(1);
    }
    for (int i = 0; i < 5; ++i) {
        const double quarter = std::round(i * 0.25 * static_cast<double>(total));
        quarterLabels_[i]->setText(QLocale().toString(static_cast<qlonglong>(std::max(1.0, quarter))));
    }
    line_ = 1;
    loadTools();
    buildIndex();
    view_->fit();
    refresh();
}

void StepThroughDialog::loadTools() {
    for (QFrame* card : toolCards_) {
        delete card;
    }
    toolCards_.clear();
    toolEyes_.clear();
    shownActiveTool_ = -2;
    const job::ProgramAnalysis& analysis = machine_.analysis();
    tools_ = machine_.isAnalyzing()
                 ? std::vector<job::StepperTool>{}
                 : job::stepperTools(analysis.spindleToolEvents, lines_.size(), analysis.tools,
                                     ToolpathCanvas::kCut.name().toStdString());
    hiddenTools_.assign(tools_.size(), false);
    toolsEmpty_->setVisible(tools_.empty());
    const bool metric = machine_.settings().metric;
    for (std::size_t i = 0; i < tools_.size(); ++i) {
        const job::StepperTool& tool = tools_[i];
        const QString color = QString::fromStdString(tool.color);
        auto* card = new QFrame;
        card->setObjectName("toolCard");
        card->setCursor(Qt::PointingHandCursor);
        card->setProperty("toolIndex", static_cast<int>(i));
        card->setToolTip(tr("Go to tool %1, line %2").arg(js::numberToString(tool.toolNumber).c_str()).arg(tool.startLine));
        card->installEventFilter(this);
        auto* row = new QHBoxLayout(card);
        auto* badge = new QLabel(QString::number(tool.index));
        badge->setFixedSize(28, 28);
        badge->setAlignment(Qt::AlignCenter);
        badge->setStyleSheet(QString("background:%1;color:%2;border-radius:4px;font-weight:bold")
                                 .arg(color, QString::fromStdString(job::readableTextColor(tool.color))));
        row->addWidget(badge);
        auto* text = new QVBoxLayout;
        QString title = QString("<b>T%1</b>").arg(js::numberToString(tool.toolNumber).c_str());
        if (!tool.comment.empty()) {
            title += QString(" <span style='color:#8b95a1'>&middot; %1</span>")
                         .arg(QString::fromStdString(tool.comment).toHtmlEscaped());
        }
        auto* name = new QLabel(title);
        name->setTextFormat(Qt::RichText);
        name->setWordWrap(true);
        name->setMinimumWidth(40);
        text->addWidget(name);
        QStringList details;
        if (tool.diameter) {
            details << QString::fromUtf8("⌀ ") +
                           (metric ? QString::fromStdString(js::toFixed(*tool.diameter, 3)) + " mm"
                                   : QString::fromStdString(js::toFixed(*tool.diameter / 25.4, 4)) + " in");
        }
        if (tool.spindleSpeed) {
            details << tr("%1 RPM").arg(localeNumber(*tool.spindleSpeed));
        }
        if (!details.isEmpty()) {
            auto* detail = new QLabel(details.join("   "));
            detail->setStyleSheet("font-size:8pt");
            text->addWidget(detail);
        }
        row->addLayout(text, 1);
        auto* range = new QLabel(QString("%1-%2").arg(tool.startLine).arg(tool.endLine));
        range->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        range->setStyleSheet("color:palette(mid)");
        row->addWidget(range);
        auto* eye = new QToolButton;
        eye->setCheckable(true);
        eye->setMinimumSize(52, 28);
        connect(eye, &QToolButton::clicked, this, [this, i] { toggleTool(static_cast<int>(i)); });
        row->addWidget(eye);
        toolList_->insertWidget(toolList_->count() - 1, card);
        toolCards_.push_back(card);
        toolEyes_.push_back(eye);
    }
    updateViewTools();
    refreshTools();
}

void StepThroughDialog::buildIndex() {
    if (cancel_) {
        *cancel_ = true;
    }
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    indexReady_ = false;
    index_ = {};
    const std::uint64_t generation = ++generation_;
    view_->setOverlay(tr("Reading positions... %1%").arg(0));
    auto text = std::make_shared<const std::string>(text_);
    auto cancel = cancel_;
    QPointer<StepThroughDialog> guard(this);
    QThreadPool::globalInstance()->start([guard, text, cancel, generation] {
        auto index = std::make_shared<job::StepIndex>(job::buildStepIndex(
            *text, {}, [cancel] { return cancel->load(); },
            [guard, generation](std::size_t done, std::size_t total) {
                const int percent = total > 0 ? static_cast<int>(100 * done / total) : 100;
                QMetaObject::invokeMethod(qApp, [guard, generation, percent] {
                    if (guard && guard->generation_ == generation && !guard->indexReady_) {
                        guard->view_->setOverlay(tr("Reading positions... %1%").arg(percent));
                    }
                });
            }));
        if (cancel->load()) {
            return;
        }
        QMetaObject::invokeMethod(qApp, [guard, generation, index] {
            if (guard) {
                guard->indexBuilt(generation, index);
            }
        });
    });
}

void StepThroughDialog::indexBuilt(std::uint64_t generation, std::shared_ptr<job::StepIndex> index) {
    if (generation != generation_) {
        return;
    }
    index_ = std::move(*index);
    indexReady_ = true;
    view_->setOverlay({});
    updateViewTools();
    refresh();
}

void StepThroughDialog::goToLine(std::size_t line) {
    stopPlaying();
    setLine(static_cast<double>(line));
}

void StepThroughDialog::setLine(double line) {
    const std::size_t total = lines_.size();
    if (total == 0) {
        return;
    }
    const double rounded = std::round(line);
    line_ = static_cast<std::size_t>(std::clamp(rounded, 1.0, static_cast<double>(total)));
    refresh();
}

void StepThroughDialog::step(int delta) {
    goToLine(static_cast<std::size_t>(
        std::max<long long>(1, static_cast<long long>(line_) + static_cast<long long>(delta))));
}

void StepThroughDialog::reset() {
    stopPlaying();
    setLine(1);
}

void StepThroughDialog::togglePlay() {
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
    refresh();
}

void StepThroughDialog::stopPlaying() {
    if (!playing_) {
        return;
    }
    playing_ = false;
    playTimer_->stop();
    refresh();
}

void StepThroughDialog::setPlaybackSpeed(double speed) {
    speed_ = speed;
    for (int i = 0; i < 4; ++i) {
        if (kSpeeds[i] == speed) {
            speeds_->button(i)->setChecked(true);
        }
    }
    if (playing_) {
        // The pace changes from here.
        playStartLine_ = static_cast<double>(line_);
        playClock_.start();
    }
}

void StepThroughDialog::tick() {
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

int StepThroughDialog::activeTool() const {
    return job::activeToolIndex(tools_, line_);
}

void StepThroughDialog::toggleTool(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= hiddenTools_.size()) {
        return;
    }
    hiddenTools_[static_cast<std::size_t>(index)] = !hiddenTools_[static_cast<std::size_t>(index)];
    updateViewTools();
    shownActiveTool_ = -2;  // restyle the cards
    refreshTools();
}

bool StepThroughDialog::toolHidden(int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < hiddenTools_.size() &&
           hiddenTools_[static_cast<std::size_t>(index)];
}

void StepThroughDialog::updateViewTools() {
    // Each tool's lines as sender lines: from those run before its first
    // line to those run through its last (buildToolFrameGroups()).
    std::vector<StepThroughView::ToolSpan> spans;
    if (indexReady_) {
        for (std::size_t i = 0; i < tools_.size(); ++i) {
            const job::StepperTool& tool = tools_[i];
            spans.push_back({index_.streamedThrough(tool.startLine - 1), index_.streamedThrough(tool.endLine),
                             QColor(QString::fromStdString(tool.color)), hiddenTools_[i]});
        }
    }
    view_->setTools(std::move(spans));
}

void StepThroughDialog::setHideProcessed(bool hide) {
    hideProcessed_ = hide;
    hideProcessedButton_->setChecked(hide);
    hideProcessedButton_->setText(hide ? tr("Show prior lines") : tr("Hide prior lines"));
    refresh();
}

void StepThroughDialog::setSearch(const QString& query) {
    matches_.clear();
    std::string needle = query.trimmed().toLower().toStdString();
    if (!needle.empty()) {
        for (std::size_t i = 0; i < lines_.size() && matches_.size() < kMaxMatches; ++i) {
            if (containsIgnoringCase(lines_[i], needle)) {
                matches_.push_back(i + 1);
            }
        }
    }
    model_->setMatches(matches_);
    matchCount_->setText(needle.empty() ? QString::fromUtf8("—")
                                        : QLocale().toString(static_cast<qlonglong>(matches_.size())));
}

void StepThroughDialog::goToNextMatch() {
    if (matches_.empty()) {
        return;
    }
    const auto next = std::upper_bound(matches_.begin(), matches_.end(), line_);
    goToLine(next != matches_.end() ? *next : matches_.front());
}

QString StepThroughDialog::positionText() const {
    // Always mm in the index; both units show 2 decimals.
    const bool metric = machine_.settings().metric;
    const gcode::Vec4 p = indexReady_ ? index_.position(line_) : gcode::Vec4{};
    const auto linear = [metric](double mm) {
        return QString::fromStdString(js::toFixed(metric ? mm : mm / 25.4, 2));
    };
    QString text = QString("X %1   Y %2   Z %3").arg(linear(p.x), linear(p.y), linear(p.z));
    if (machine_.analysis().usedAxes.find('A') != std::string::npos) {
        text += QString::fromUtf8("   A %1°").arg(QString::fromStdString(js::toFixed(p.a, 2)));
    }
    return text;
}

job::StepModalReadout StepThroughDialog::modalReadout() const {
    return indexReady_ ? index_.modals(line_) : job::StepModalReadout{};
}

void StepThroughDialog::scrollSourceToCurrent() {
    // Re-centred only when the row has left the view.
    const QModelIndex at = model_->index(static_cast<int>(line_) - 1);
    if (!at.isValid()) {
        return;
    }
    const QRect row = source_->visualRect(at);
    if (!source_->viewport()->rect().contains(row)) {
        source_->scrollTo(at, QAbstractItemView::PositionAtCenter);
    }
}

void StepThroughDialog::refresh() {
    const std::size_t total = lines_.size();
    model_->setCurrent(line_);
    if (!scrubbing_) {
        scrollSourceToCurrent();
    }
    {
        const QSignalBlocker block(scrubber_);
        scrubber_->setValue(static_cast<int>(line_));
    }
    scrubber_->setEnabled(total > 0);
    position_->setText(QString("<b>%1</b> / %2")
                           .arg(QLocale().toString(static_cast<qlonglong>(line_)),
                                QLocale().toString(static_cast<qlonglong>(total))));
    play_->setEnabled(total > 0);
    play_->setChecked(playing_);
    play_->setText(playing_ ? tr("Pause") : tr("Play"));
    reset_->setEnabled(total > 0 && line_ != 1);
    for (int i = 0; i < 4; ++i) {
        steps_[i]->setEnabled(kSteps[i] < 0 ? line_ > 1 : line_ < total);
    }
    view_->seekTo(indexReady_ ? index_.position(line_) : gcode::Vec4{}, indexReady_ ? index_.streamedThrough(line_) : 0,
                  hideProcessed_);
    refreshTools();
    refreshReadout();
}

void StepThroughDialog::refreshTools() {
    const int active = activeTool();
    if (active == shownActiveTool_) {
        return;
    }
    shownActiveTool_ = active;
    for (std::size_t i = 0; i < toolCards_.size(); ++i) {
        const QString color = QString::fromStdString(tools_[i].color);
        const bool isActive = static_cast<int>(i) == active;
        const bool hidden = hiddenTools_[i];
        toolCards_[i]->setStyleSheet(
            QString("QFrame#toolCard { border:%1px solid %2; border-left:4px solid %3; border-radius:6px; }")
                .arg(isActive ? 2 : 1)
                .arg(isActive ? color : QStringLiteral("palette(mid)"), color));
        auto* dim = qobject_cast<QGraphicsOpacityEffect*>(toolCards_[i]->graphicsEffect());
        if (hidden && !dim) {
            dim = new QGraphicsOpacityEffect(toolCards_[i]);
            dim->setOpacity(0.5);
            toolCards_[i]->setGraphicsEffect(dim);
        } else if (!hidden && dim) {
            toolCards_[i]->setGraphicsEffect(nullptr);
        }
        QToolButton* eye = toolEyes_[i];
        eye->setChecked(!hidden);
        eye->setText(hidden ? tr("Hidden") : tr("Shown"));
        eye->setToolTip((hidden ? tr("Show tool %1 toolpath") : tr("Hide tool %1 toolpath"))
                            .arg(js::numberToString(tools_[i].toolNumber).c_str()));
        eye->setStyleSheet(hidden ? QString()
                                  : QString("QToolButton { background:%1; color:%2; border-radius:4px; }")
                                        .arg(color, QString::fromStdString(job::readableTextColor(tools_[i].color))));
    }
}

void StepThroughDialog::refreshReadout() {
    // The whole current line, and the status: refreshed at rest only (the
    // line changes too fast to read while playing or scrubbing).
    const bool idle = !playing_ && !scrubbing_;
    lineReadout_->setEnabled(idle);
    const bool metric = machine_.settings().metric;
    positionTitle_->setText(sectionTitle(metric ? tr("Position (Work, mm)") : tr("Position (Work, in)")));
    positionValues_->setText(positionText());
    if (idle) {
        lineNumber_->setText(QString::number(line_));
        lineText_->setText(line_ >= 1 && line_ <= lines_.size() ? QString::fromStdString(lines_[line_ - 1]) : QString());
    }
    const job::StepModalReadout current = modalReadout();
    const job::StepModalReadout previous =
        indexReady_ && line_ > 1 ? index_.modals(line_ - 1) : job::StepModalReadout{};
    for (int i = 0; i < 11; ++i) {
        const std::size_t at = static_cast<std::size_t>(i);
        const bool changed = indexReady_ && line_ > 1 && previous[at] != current[at];
        const QString value = current[at].empty() ? QString::fromUtf8("—") : QString::fromStdString(current[at]);
        modalCells_[i]->setText(QString("<div style='font-size:7pt'>%1</div><div style='font-family:monospace'>%2%3%4</div>")
                                    .arg(tr(job::kStepModalLabels[at]).toUpper(), changed ? "<b>" : "",
                                         value.toHtmlEscaped(), changed ? "</b>" : ""));
        modalCells_[i]->setStyleSheet(changed ? "QLabel { border:1px solid #3b82f6; background:rgba(59,130,246,50);"
                                                " border-radius:3px; padding:2px; }"
                                              : "QLabel { border:1px solid palette(mid); border-radius:3px; padding:2px; }");
    }
}

bool StepThroughDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        const QVariant index = watched->property("toolIndex");
        if (index.isValid()) {
            const int i = index.toInt();
            if (i >= 0 && static_cast<std::size_t>(i) < tools_.size()) {
                goToLine(tools_[static_cast<std::size_t>(i)].startLine);
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

}  // namespace gs::app
