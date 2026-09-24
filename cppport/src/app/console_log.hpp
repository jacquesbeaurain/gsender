#pragma once

// The console's lines (features/Console: consoleStore, consoleIngest,
// definitions): each kept with why it was written - G-code sent, the
// board's responses, system messages, warnings, errors, alarms - the last
// 1000 of them, shown by filter. One log feeds the console panel and its
// pop-out; the typed commands' history is kept with it.

#include "gs/controller/events.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <deque>
#include <vector>

class QTimer;

namespace gs::app {

enum class ConsoleType { Gcode, Response, System, Warning, Error, Alarm };
enum class ConsoleFilter { All, Gcode, Response, System, Faults };

struct ConsoleMessage {
    quint64 id = 0;
    QString text;
    ConsoleType type = ConsoleType::Response;
};

// classifyControllerRead(): what the board said, by its text - an alarm
// before an error before a [MSG:] (a warning when it says WARN).
ConsoleType classifyRead(const QString& line);
// classifyControllerWrite(): what the server says about itself is system
// output, anything put on the wire G-code.
ConsoleType classifyWrite(controller::WriteSource source);
// matchesFilter(): Faults gathers warnings, errors and alarms.
bool matchesFilter(const ConsoleMessage& message, ConsoleFilter filter);

class ConsoleLog final : public QObject {
    Q_OBJECT
public:
    static constexpr int kLimit = 1000;      // CONSOLE_HISTORY_LIMIT
    static constexpr int kInputLimit = 300;  // MAX_TERMINAL_INPUT_ARRAY_SIZE
    static constexpr int kFlushMs = 30;      // FLUSH_INTERVAL_MS

    explicit ConsoleLog(QObject* parent = nullptr);

    // consoleWrite(): empty lines are dropped; lines are handed on in
    // batches every 30 ms (a job can write hundreds in a moment).
    void write(const QString& text, ConsoleType type = ConsoleType::Response);
    void flush();  // the waiting lines now
    void clear();  // and the waiting ones
    const std::deque<ConsoleMessage>& messages() const noexcept { return messages_; }
    // The last `count` lines' texts, whatever the filter (Copy last 50).
    QStringList lastTexts(int count) const;

    // The command line's history, the last 300.
    void addInput(const QString& command);
    const QStringList& inputHistory() const noexcept { return inputs_; }

Q_SIGNALS:
    // `count` lines were added at the end and `dropped` removed from the
    // front.
    void appended(int count, int dropped);
    void cleared();

private:
    std::deque<ConsoleMessage> messages_;
    std::vector<ConsoleMessage> pending_;
    QTimer* timer_;
    quint64 nextId_ = 0;
    QStringList inputs_;
};

}  // namespace gs::app
