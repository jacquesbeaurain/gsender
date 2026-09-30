#include "gcode_editor_model.hpp"

#include "backend.hpp"
#include "gcode_highlighter.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"
#include "gs/job/program_analysis.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <QClipboard>
#include <QGuiApplication>

#include <algorithm>
#include <cmath>

namespace gs::ui {

GcodeEditorModel::GcodeEditorModel(QObject* parent)
    : QAbstractListModel(parent), machine_(UiBackend::instance()->machine()) {
    for (auto signal : {&app::Machine::workflowChanged, &app::Machine::senderStatusChanged,
                        &app::Machine::connectionChanged}) {
        connect(&machine_, signal, this, &GcodeEditorModel::followJob);
    }
}

int GcodeEditorModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant GcodeEditorModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= count()) {
        return {};
    }
    const int row = index.row();
    switch (role) {
        case TextRole: return lines_[row];
        // Coloured as the row is shown, and plain while a job runs (upstream).
        case StyledRole:
            return running_ ? lines_[row].toHtmlEscaped() : app::gcodeStyledText(lines_[row], machine_.settings().darkMode);
        case SelectedRole: return selected_.count(row) != 0;
        case MatchRole: return matches_.contains(static_cast<std::size_t>(row));
        case CurrentMatchRole: return row == currentMatchRow();
        case StatusRole:
            if (!running_ || runningRow_ < 0) {
                return QStringLiteral("none");
            }
            if (row < runningRow_) {
                return QStringLiteral("processed");
            }
            return row <= runningRow_ + 2 ? QStringLiteral("current") : QStringLiteral("upcoming");
        default: return {};
    }
}

QHash<int, QByteArray> GcodeEditorModel::roleNames() const {
    return {{TextRole, "text"},           {StyledRole, "styled"},
            {SelectedRole, "selected"},   {MatchRole, "matched"},
            {CurrentMatchRole, "currentMatch"}, {StatusRole, "status"}};
}

void GcodeEditorModel::rowsChanged(const QList<int>& roles) {
    if (count() > 0) {
        Q_EMIT dataChanged(index(0), index(count() - 1), roles);
    }
}

QString GcodeEditorModel::jobState() const {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return {};
    }
    switch (c->workflow().state()) {
        case controller::WorkflowState::Running: return QStringLiteral("Running");
        case controller::WorkflowState::Paused: return QStringLiteral("Paused");
        default: return {};
    }
}

bool GcodeEditorModel::jobRunning() const {
    return !jobState().isEmpty();
}

int GcodeEditorModel::jumpRow(const QString& text) const {
    const double line = js::stringToNumber(text.trimmed().toStdString());
    if (!(line >= 1) || lines_.isEmpty()) {
        return -1;
    }
    return static_cast<int>(std::min(std::floor(line), static_cast<double>(lines_.size()))) - 1;
}

void GcodeEditorModel::load() {
    beginResetModel();
    // One line per row: CR LF and lone CR endings become LF.
    const std::string& program = machine_.programText();
    lines_.clear();
    for (const std::string_view line : str::splitLines(program)) {
        lines_ << QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size()));
    }
    original_ = lines_;
    senderLines_ = job::senderLineNumbers(program);
    selected_.clear();
    lastSelected_ = -1;
    endResetModel();
    findMatches();
    followJob();
    Q_EMIT linesChanged();
    Q_EMIT selectionChanged();
}

void GcodeEditorModel::setLine(int row, const QString& text) {
    if (running_ || row < 0 || row >= count() || lines_[row] == text) {
        return;
    }
    lines_[row] = text;
    Q_EMIT dataChanged(index(row), index(row), {TextRole, StyledRole});
    Q_EMIT linesChanged();
}

void GcodeEditorModel::toggleSelected(int row, bool range) {
    if (running_ || row < 0 || row >= count()) {
        return;
    }
    if (range && lastSelected_ >= 0) {
        for (int i = std::min(lastSelected_, row); i <= std::max(lastSelected_, row); ++i) {
            selected_.insert(i);
        }
        lastSelected_ = row;
    } else if (selected_.erase(row) != 0) {
        lastSelected_ = selected_.empty() ? -1 : (lastSelected_ == row ? *selected_.rbegin() : lastSelected_);
    } else {
        selected_.insert(row);
        lastSelected_ = row;
    }
    rowsChanged({SelectedRole});
    Q_EMIT selectionChanged();
}

void GcodeEditorModel::clearSelection() {
    selected_.clear();
    lastSelected_ = -1;
    rowsChanged({SelectedRole});
    Q_EMIT selectionChanged();
}

void GcodeEditorModel::toggleSelectAll() {
    if (running_) {
        return;
    }
    if (selectedCount() == count()) {
        selected_.clear();
    } else {
        for (int i = 0; i < count(); ++i) {
            selected_.insert(i);
        }
    }
    lastSelected_ = -1;
    rowsChanged({SelectedRole});
    Q_EMIT selectionChanged();
}

void GcodeEditorModel::deleteSelected() {
    if (running_ || selected_.empty()) {
        return;
    }
    beginResetModel();
    for (auto it = selected_.rbegin(); it != selected_.rend(); ++it) {
        lines_.removeAt(*it);
    }
    selected_.clear();
    lastSelected_ = -1;
    endResetModel();
    findMatches();
    Q_EMIT linesChanged();
    Q_EMIT selectionChanged();
}

QString GcodeEditorModel::copy() {
    QStringList picked;
    if (selected_.empty()) {
        picked = lines_;
    } else {
        for (const int row : selected_) {
            picked << lines_[row];
        }
    }
    QGuiApplication::clipboard()->setText(picked.join('\n'));
    return selected_.empty() ? tr("G-code has been copied to your clipboard")
                             : tr("%1 line(s) copied to clipboard").arg(selected_.size());
}

bool GcodeEditorModel::revertChanges() {
    if (running_ || !hasChanges()) {
        return false;
    }
    beginResetModel();
    lines_ = original_;
    selected_.clear();
    lastSelected_ = -1;
    endResetModel();
    findMatches();
    Q_EMIT linesChanged();
    Q_EMIT selectionChanged();
    return true;
}

bool GcodeEditorModel::save() {
    if (running_ || !hasChanges()) {
        return false;
    }
    const std::string edited = text().toStdString();
    original_ = lines_;
    senderLines_ = job::senderLineNumbers(edited);
    machine_.loadProgram(machine_.programName(), edited, machine_.programPath());
    Q_EMIT linesChanged();
    return true;
}

void GcodeEditorModel::setSearch(const QString& query) {
    query_ = query;
    findMatches();
}

void GcodeEditorModel::findMatches() {
    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(lines_.size()));
    for (const QString& line : lines_) {
        lines.push_back(line.toStdString());
    }
    matches_.find(lines, query_.toStdString());
    rowsChanged({MatchRole, CurrentMatchRole});
    Q_EMIT searchChanged();
}

int GcodeEditorModel::currentMatchRow() const {
    const std::optional<std::size_t> line = matches_.currentLine();
    return line ? static_cast<int>(*line) : -1;
}

void GcodeEditorModel::nextMatch() {
    if (!matches_.empty()) {
        matches_.next();
        rowsChanged({CurrentMatchRole});
        Q_EMIT searchChanged();
    }
}

void GcodeEditorModel::previousMatch() {
    if (!matches_.empty()) {
        matches_.previous();
        rowsChanged({CurrentMatchRole});
        Q_EMIT searchChanged();
    }
}

void GcodeEditorModel::followJob() {
    const bool running = jobRunning();
    int row = -1;
    controller::Controller* c = machine_.controller();
    if (running && c && !senderLines_.empty()) {
        // The sender's running line as a file line (upstream compared the
        // sender's count with file lines directly, drifting by the blank
        // lines before it).
        const std::int64_t at = c->sender().currentLineRunning();
        if (at >= 0) {
            row = static_cast<int>(senderLines_[std::min(static_cast<std::size_t>(at), senderLines_.size() - 1)]) - 1;
        }
    }
    if (running != running_ || row != runningRow_) {
        const bool styling = running != running_;
        running_ = running;
        runningRow_ = row;
        rowsChanged(styling ? QList<int>{StatusRole, StyledRole} : QList<int>{StatusRole});
        Q_EMIT jobChanged();
    }
}

}  // namespace gs::ui
