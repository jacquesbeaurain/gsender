#pragma once

// The Console (features/Console): the machine's traffic as the log keeps it
// - each line with its type's mark and colour on a dark surface - filtered
// to All, G-code, Responses, System or Faults; copying the last 50 lines,
// clearing, popping out into a window of its own; and the command line with
// its history. "Not connected to a device" while there is no machine.

#include "console_log.hpp"

#include <QPointer>
#include <QWidget>

class QButtonGroup;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;

namespace gs::app {

class Machine;

class ConsolePanel final : public QWidget {
    Q_OBJECT
public:
    // `popout`: the pop-out window's console (it has no pop-out of its own).
    explicit ConsolePanel(Machine& machine, bool popout = false, QWidget* parent = nullptr);

    ConsoleFilter filter() const noexcept { return filter_; }
    void setFilter(ConsoleFilter filter);
    // The lines shown, as text (tests).
    QStringList shownLines() const;
    // Copy last 50 messages / Clear console / Pop out console.
    void copyLast();
    void clearAll();
    ConsolePanel* popOut();  // the pop-out's console (made on first use)
    // What the command line does with Enter.
    void submit(const QString& command);
    // The commands typed, oldest first (the diagnostics' terminal history).
    const QStringList& history() const;

Q_SIGNALS:
    // What upstream toasts (copied, cleared).
    void notice(const QString& text, bool success);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void render();
    void appendLines(int count);
    void insert(const ConsoleMessage& message);
    void followOrOffer();
    void placeLatest();
    void refreshConnection();

    Machine& machine_;
    ConsoleLog& log_;
    ConsoleFilter filter_ = ConsoleFilter::All;
    QButtonGroup* filters_;
    QPlainTextEdit* output_;
    QToolButton* latest_;  // "Scroll to latest message"
    bool atBottom_ = true;  // following the output
    QLineEdit* input_;
    int historyIndex_ = -1;  // -1: none chosen
    QPointer<QWidget> popoutWindow_;
    ConsolePanel* popout_ = nullptr;
};

}  // namespace gs::app
