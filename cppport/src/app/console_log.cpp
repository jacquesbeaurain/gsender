#include "console_log.hpp"

#include <QRegularExpression>
#include <QTimer>

namespace gs::app {

ConsoleType classifyRead(const QString& line) {
    if (line.contains("ALARM:")) {
        return ConsoleType::Alarm;
    }
    if (line.contains("error:")) {
        return ConsoleType::Error;
    }
    if (line.contains("[MSG:")) {
        static const QRegularExpression kWarning(R"(\[MSG:\s*(WARN|WARNING))",
                                                 QRegularExpression::CaseInsensitiveOption);
        return kWarning.match(line).hasMatch() ? ConsoleType::Warning : ConsoleType::System;
    }
    return ConsoleType::Response;
}

ConsoleType classifyWrite(controller::WriteSource source) {
    return source == controller::WriteSource::Server ? ConsoleType::System : ConsoleType::Gcode;
}

bool matchesFilter(const ConsoleMessage& message, ConsoleFilter filter) {
    switch (filter) {
        case ConsoleFilter::All: return true;
        case ConsoleFilter::Faults:
            return message.type == ConsoleType::Warning || message.type == ConsoleType::Error ||
                   message.type == ConsoleType::Alarm;
        case ConsoleFilter::Gcode: return message.type == ConsoleType::Gcode;
        case ConsoleFilter::Response: return message.type == ConsoleType::Response;
        case ConsoleFilter::System: return message.type == ConsoleType::System;
    }
    return true;
}

ConsoleLog::ConsoleLog(QObject* parent) : QObject(parent) {
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(kFlushMs);
    connect(timer_, &QTimer::timeout, this, &ConsoleLog::flush);
}

void ConsoleLog::write(const QString& text, ConsoleType type) {
    if (text.isEmpty()) {
        return;
    }
    pending_.push_back({++nextId_, text, type});
    // A hidden window flushes rarely: what would be trimmed anyway goes now.
    if (pending_.size() > static_cast<std::size_t>(kLimit)) {
        pending_.erase(pending_.begin(), pending_.end() - kLimit);
    }
    if (!timer_->isActive()) {
        timer_->start();
    }
}

void ConsoleLog::flush() {
    timer_->stop();
    if (pending_.empty()) {
        return;
    }
    const int count = static_cast<int>(pending_.size());
    for (ConsoleMessage& message : pending_) {
        messages_.push_back(std::move(message));
    }
    pending_.clear();
    int dropped = 0;
    while (messages_.size() > static_cast<std::size_t>(kLimit)) {
        messages_.pop_front();
        ++dropped;
    }
    Q_EMIT appended(count, dropped);
}

void ConsoleLog::clear() {
    timer_->stop();
    pending_.clear();
    messages_.clear();
    Q_EMIT cleared();
}

QStringList ConsoleLog::lastTexts(int count) const {
    QStringList texts;
    const std::size_t from = messages_.size() > static_cast<std::size_t>(count) ? messages_.size() - count : 0;
    for (std::size_t i = from; i < messages_.size(); ++i) {
        texts << messages_[i].text;
    }
    return texts;
}

void ConsoleLog::addInput(const QString& command) {
    inputs_ << command;
    while (inputs_.size() > kInputLimit) {
        inputs_.removeFirst();
    }
}

}  // namespace gs::app
