#pragma once

// The Console (features/Console) for QML: the machine's traffic as the
// console log keeps it, filtered (All, G-code, Responses, System, Faults), as
// a list model - rows added and dropped as the log is, so the view keeps its
// place - with Copy last 50, Clear, and the command line with its history.

#include "console_log.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <deque>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class ConsoleModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    // "all", "gcode", "response", "system", "faults".
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Any lines at all, whatever the filter ("No messages match this filter").
    Q_PROPERTY(bool hasMessages READ hasMessages NOTIFY countChanged)

public:
    enum Role { TextRole = Qt::UserRole + 1, KindRole };

    explicit ConsoleModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool connected() const;
    QString filter() const;
    void setFilter(const QString& filter);
    int count() const { return static_cast<int>(rows_.size()); }
    bool hasMessages() const;

    // The texts shown (tests).
    Q_INVOKABLE QStringList shownLines() const;
    // Copy last 50 messages: the raw stream, not the view. The count copied.
    Q_INVOKABLE int copyLast();
    Q_INVOKABLE void clear();
    // Enter: sent and shown as sent, kept in the history. False when not.
    Q_INVOKABLE bool submit(const QString& text);
    // Up/Down through the history (ConsoleInput's navigateHistory): the
    // line's new text, or undefined to leave it.
    Q_INVOKABLE QVariant historyStep(bool up);
    // Backspace on an (almost) empty line: nothing chosen any more.
    Q_INVOKABLE void resetHistory() { historyIndex_ = -1; }

Q_SIGNALS:
    void connectedChanged();
    void filterChanged();
    void countChanged();

private:
    void rebuild();
    void append(int count, int dropped);

    app::Machine& machine_;
    app::ConsoleLog& log_;
    app::ConsoleFilter filter_ = app::ConsoleFilter::All;
    std::deque<app::ConsoleMessage> rows_;
    int historyIndex_ = -1;
};

}  // namespace gs::ui
