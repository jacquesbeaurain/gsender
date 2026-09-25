#include "console_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include <QClipboard>
#include <QGuiApplication>

#include <algorithm>

namespace gs::ui {
namespace {

constexpr int kCopyLimit = 50;  // COPY_HISTORY_LIMIT

const char* kindOf(app::ConsoleType type) {
    switch (type) {
        case app::ConsoleType::Gcode: return "gcode";
        case app::ConsoleType::Response: return "response";
        case app::ConsoleType::System: return "system";
        case app::ConsoleType::Warning: return "warning";
        case app::ConsoleType::Error: return "error";
        case app::ConsoleType::Alarm: return "alarm";
    }
    return "response";
}

constexpr std::pair<app::ConsoleFilter, const char*> kFilters[] = {
    {app::ConsoleFilter::All, "all"},         {app::ConsoleFilter::Gcode, "gcode"},
    {app::ConsoleFilter::Response, "response"}, {app::ConsoleFilter::System, "system"},
    {app::ConsoleFilter::Faults, "faults"},
};

}  // namespace

ConsoleModel::ConsoleModel(QObject* parent)
    : QAbstractListModel(parent),
      machine_(UiBackend::instance()->machine()),
      log_(machine_.consoleLog()) {
    connect(&log_, &app::ConsoleLog::appended, this, &ConsoleModel::append);
    connect(&log_, &app::ConsoleLog::cleared, this, &ConsoleModel::rebuild);
    connect(&machine_, &app::Machine::connectionChanged, this, &ConsoleModel::connectedChanged);
    rebuild();
}

int ConsoleModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant ConsoleModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= count()) {
        return {};
    }
    const app::ConsoleMessage& message = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case TextRole: return message.text;
        case KindRole: return QString::fromLatin1(kindOf(message.type));
        default: return {};
    }
}

QHash<int, QByteArray> ConsoleModel::roleNames() const {
    return {{TextRole, "text"}, {KindRole, "kind"}};
}

bool ConsoleModel::connected() const {
    return machine_.isConnected();
}

QString ConsoleModel::filter() const {
    for (const auto& [id, name] : kFilters) {
        if (id == filter_) {
            return QString::fromLatin1(name);
        }
    }
    return QStringLiteral("all");
}

void ConsoleModel::setFilter(const QString& filter) {
    for (const auto& [id, name] : kFilters) {
        if (filter == QLatin1String(name) && id != filter_) {
            filter_ = id;
            rebuild();
            Q_EMIT filterChanged();
        }
    }
}

bool ConsoleModel::hasMessages() const {
    return !log_.messages().empty();
}

void ConsoleModel::rebuild() {
    beginResetModel();
    rows_.clear();
    for (const app::ConsoleMessage& message : log_.messages()) {
        if (app::matchesFilter(message, filter_)) {
            rows_.push_back(message);
        }
    }
    endResetModel();
    Q_EMIT countChanged();
}

void ConsoleModel::append(int count, int /*dropped*/) {
    const auto& messages = log_.messages();
    // What the log dropped from its front goes from ours.
    if (!rows_.empty()) {
        const quint64 first = messages.empty() ? ~quint64{0} : messages.front().id;
        std::size_t stale = 0;
        while (stale < rows_.size() && rows_[stale].id < first) {
            ++stale;
        }
        if (stale > 0) {
            beginRemoveRows({}, 0, static_cast<int>(stale) - 1);
            rows_.erase(rows_.begin(), rows_.begin() + static_cast<std::ptrdiff_t>(stale));
            endRemoveRows();
        }
    }
    std::vector<app::ConsoleMessage> added;
    const std::size_t take = std::min<std::size_t>(static_cast<std::size_t>(count), messages.size());
    for (auto it = messages.end() - static_cast<std::ptrdiff_t>(take); it != messages.end(); ++it) {
        if (app::matchesFilter(*it, filter_)) {
            added.push_back(*it);
        }
    }
    if (!added.empty()) {
        const int start = this->count();
        beginInsertRows({}, start, start + static_cast<int>(added.size()) - 1);
        rows_.insert(rows_.end(), added.begin(), added.end());
        endInsertRows();
    }
    Q_EMIT countChanged();
}

QStringList ConsoleModel::shownLines() const {
    QStringList lines;
    for (const app::ConsoleMessage& message : rows_) {
        lines << message.text;
    }
    return lines;
}

int ConsoleModel::copyLast() {
    const QStringList last = log_.lastTexts(kCopyLimit);
    if (!last.isEmpty()) {
        QGuiApplication::clipboard()->setText(last.join('\n'));
    }
    return static_cast<int>(last.size());
}

void ConsoleModel::clear() {
    log_.clear();
}

bool ConsoleModel::submit(const QString& text) {
    const QString command = text.trimmed();
    if (command.isEmpty() || !machine_.isConnected()) {
        return false;
    }
    machine_.sendConsoleLine(command);
    // The command itself is not echoed back: shown here as sent.
    log_.write(command, app::ConsoleType::Gcode);
    log_.addInput(command);
    historyIndex_ = -1;
    return true;
}

QVariant ConsoleModel::historyStep(bool up) {
    const QStringList& history = log_.inputHistory();
    const int size = static_cast<int>(history.size());
    if (size == 0) {
        return {};
    }
    if (up) {
        // From nothing chosen: the latest command.
        historyIndex_ = historyIndex_ == -1 ? size - 1 : std::max(0, historyIndex_ - 1);
    } else if (historyIndex_ == -1 || historyIndex_ >= size - 1) {
        // Down past the newest clears the line.
        historyIndex_ = -1;
        return QString();
    } else {
        ++historyIndex_;
    }
    return history[historyIndex_];
}

}  // namespace gs::ui
