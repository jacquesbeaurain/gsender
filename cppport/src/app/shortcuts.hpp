#pragma once

// gSender's keyboard shortcuts: the command ids, titles, categories and
// default keys of its shortcut table, the user's changes on top
// (app.shortcuts), and an application-wide key filter that runs them while
// the main window is active and the focus is not in a text field. Jogging
// shortcuts run while held: press starts, release stops.

#include <QKeySequence>
#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <vector>

class QKeyEvent;
class QWidget;

namespace gs::app {

class Machine;

struct ShortcutAction {
    QString id;           // gSender's command id, e.g. "JOG_X_P"
    QString title;
    QString category;
    QString defaultKeys;  // QKeySequence portable text; empty: unbound
    bool hold = false;    // runs while held (jogging)
    bool grblHalOnly = false;
};

// Every action the port has, in upstream's table order.
const std::vector<ShortcutAction>& shortcutActions();
const ShortcutAction* findShortcutAction(const QString& id);

// A key event as shortcuts see it: Shift dropped from symbols typed with it
// ("~" rather than "Shift+~", as Mousetrap binds characters), keypad
// flag dropped.
QKeyCombination shortcutKey(const QKeyEvent& event);

class ShortcutManager final : public QObject {
    Q_OBJECT

public:
    // Shortcuts run only while `window` is the active window.
    ShortcutManager(Machine& machine, QWidget& window, QObject* parent = nullptr);
    ~ShortcutManager() override;

    // The effective binding: the user's, else the default.
    QKeySequence keys(const QString& id) const;
    bool isActive(const QString& id) const;
    bool enabled() const;  // the global switch (TOGGLE_SHORTCUTS)

    // What an action does. Hold actions also get a release.
    void setHandler(const QString& id, std::function<void()> press, std::function<void()> release = {});
    // The action bound to `key`, if any (inactive bindings excluded).
    QString actionFor(QKeyCombination key) const;
    // Runs an action's press as its key would (TOGGLE_SHORTCUTS aside, only
    // while shortcuts are enabled). False when nothing handles it.
    bool trigger(const QString& id);
    void release();  // ends a held action (jog)

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Handler {
        std::function<void()> press;
        std::function<void()> release;
    };

    void rebuild();
    bool typingInto(QObject* watched) const;

    Machine& machine_;
    QWidget& window_;
    std::map<QString, Handler> handlers_;
    std::map<int, QString> bindings_;  // QKeyCombination::toCombined() -> action
    QString held_;
    int heldKey_ = 0;  // Qt::Key of the held action
};

}  // namespace gs::app
