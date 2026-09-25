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
    bool defaultActive = true;  // macros start switched off, as upstream
};

inline const QString kMacroCategory = QStringLiteral("Macros");

// Every action the port has, in upstream's table order.
const std::vector<ShortcutAction>& shortcutActions();
const ShortcutAction* findShortcutAction(const QString& id);
// The table followed by one action per macro (its id, its name, category
// "Macros"): unbound and switched off until the user binds it, as upstream
// adds macros to its commandKeys.
std::vector<ShortcutAction> shortcutActions(Machine& machine);
const ShortcutAction* findShortcutAction(const std::vector<ShortcutAction>& actions, const QString& id);

// A key event as shortcuts see it: Shift dropped from symbols typed with it
// ("~" rather than "Shift+~", as Mousetrap binds characters), keypad
// flag dropped.
QKeyCombination shortcutKey(const QKeyEvent& event);

// Where shortcuts apply: the window (its deactivation releases a held jog),
// whether a key event is the window's, whether the window is the active one,
// and whether the focus is in a text field (the keys are its then).
struct ShortcutScope {
    QObject* window = nullptr;
    std::function<bool(QObject* watched)> owns;
    std::function<bool()> active;
    std::function<bool()> typing;
};

class ShortcutManager final : public QObject {
    Q_OBJECT

public:
    // Shortcuts run only while `window` is the active window.
    ShortcutManager(Machine& machine, QWidget& window, QObject* parent = nullptr);
    ShortcutManager(Machine& machine, ShortcutScope scope, QObject* parent = nullptr);
    ~ShortcutManager() override;

    // The effective binding: the user's, else the default.
    QKeySequence keys(const QString& id) const;
    bool isActive(const QString& id) const;
    bool enabled() const;  // the global switch (TOGGLE_SHORTCUTS)

    // What an action does. Hold actions also get a release.
    void setHandler(const QString& id, std::function<void()> press, std::function<void()> release = {});
    // What a macro's shortcut does (given the macro id).
    void setMacroHandler(std::function<void(const QString& id)> handler) { macroHandler_ = std::move(handler); }
    // The action bound to `key`, if any (inactive bindings excluded).
    QString actionFor(QKeyCombination key) const;
    // The shortcuts that work now - bound, on, handled, grblHAL's own only
    // on grblHAL - in the table's order (the keyboard map).
    struct ActiveShortcut {
        QString category;
        QString title;
        QString keys;  // as the platform writes them
    };
    std::vector<ActiveShortcut> activeShortcuts() const;
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
    const ShortcutAction* action(const QString& id) const;

    Machine& machine_;
    ShortcutScope scope_;
    std::vector<ShortcutAction> actions_;  // the table and the macros
    std::map<QString, Handler> handlers_;
    std::function<void(const QString&)> macroHandler_;
    std::map<int, QString> bindings_;  // QKeyCombination::toCombined() -> action
    QString held_;
    int heldKey_ = 0;  // Qt::Key of the held action
};

}  // namespace gs::app
