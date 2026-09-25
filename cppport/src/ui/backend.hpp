#pragma once

// What the QML UI binds to: the application's Machine seen as properties,
// signals and invokable actions. QML stays declarative; decisions stay in
// C++ (here, the core and the app's services), where tests reach them.
//
// One instance per UI, registered before the QML loads (setInstance) and
// served to QML as the `Backend` singleton.

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace gs::app {
class Jogger;
class Machine;
class NotificationCenter;
class ShortcutManager;
}

namespace gs::ui {

class UiBackend final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Backend)
    QML_SINGLETON

    // The connection (the top bar's connection button).
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectionChanged)
    Q_PROPERTY(QString portLabel READ portLabel NOTIFY connectionChanged)
    Q_PROPERTY(QString firmwareLabel READ firmwareLabel NOTIFY connectionChanged)
    // "usb", "ethernet" or "simulator": the connection's icon.
    Q_PROPERTY(QString connectionKind READ connectionKind NOTIFY connectionChanged)
    // The machine state (the status pill): Grbl's state ("" disconnected),
    // what upstream shows for it, and the alarm code.
    Q_PROPERTY(QString activeState READ activeState NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString alarmCode READ alarmCode NOTIFY stateChanged)
    // The loaded job.
    Q_PROPERTY(bool hasProgram READ hasProgram NOTIFY programChanged)
    Q_PROPERTY(QString programName READ programName NOTIFY programChanged)
    // workspace.enableDarkMode: the Workshop dark theme.
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY appSettingsChanged)
    // The tool area's optional tabs: Spindle/Laser, Coolant, Rotary.
    Q_PROPERTY(bool spindleFunctions READ spindleFunctions NOTIFY appSettingsChanged)
    Q_PROPERTY(bool coolantFunctions READ coolantFunctions NOTIFY appSettingsChanged)
    Q_PROPERTY(bool rotaryTab READ rotaryTab NOTIFY appSettingsChanged)
    // The notifications (NotificationsArea): {message, type, read, ago},
    // newest first, and the unread errors the bell counts.
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)
    Q_PROPERTY(int unreadErrors READ unreadErrors NOTIFY notificationsChanged)
    // The Helper's info panel (features/Helper): a card over the top left
    // with a title, an explanation and a resource link, until closed.
    Q_PROPERTY(bool helperVisible READ helperVisible NOTIFY helperChanged)
    Q_PROPERTY(QString helperTitle READ helperTitle NOTIFY helperChanged)
    Q_PROPERTY(QString helperText READ helperText NOTIFY helperChanged)  // rich text
    Q_PROPERTY(QString helperLink READ helperLink NOTIFY helperChanged)
    // Accessibility's "Show keyboard shortcut map": the overlay of the
    // shortcuts that work now; whether shortcuts are on at all.
    Q_PROPERTY(bool keyboardMap READ keyboardMap WRITE setKeyboardMap NOTIFY appSettingsChanged)
    Q_PROPERTY(bool shortcutsEnabled READ shortcutsEnabled NOTIFY appSettingsChanged)

public:
    explicit UiBackend(app::Machine& machine, QObject* parent = nullptr);

    // The instance QML's `Backend` is (set before loading the QML).
    static void setInstance(UiBackend* backend);
    static UiBackend* instance();
    static UiBackend* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    app::Machine& machine() noexcept { return machine_; }
    // The jog presets and tap/hold jogging, shared by the jog controls and
    // the keyboard shortcuts.
    app::Jogger& jogger() noexcept { return *jogger_; }

    bool connected() const;
    bool connecting() const;
    QString portLabel() const;
    QString firmwareLabel() const;
    QString connectionKind() const;
    QString activeState() const;
    QString stateText() const;
    QString alarmCode() const;
    bool hasProgram() const;
    QString programName() const;
    bool darkMode() const;
    bool spindleFunctions() const;
    bool coolantFunctions() const;
    bool rotaryTab() const;
    void setDarkMode(bool dark);
    QVariantList notifications() const;
    int unreadErrors() const;
    bool helperVisible() const noexcept { return helperVisible_; }
    QString helperTitle() const { return helperTitle_; }
    QString helperText() const { return helperText_; }
    QString helperLink() const { return helperLink_; }
    app::NotificationCenter& notificationCenter() noexcept { return *notifications_; }
    bool keyboardMap() const;
    void setKeyboardMap(bool shown);
    bool shortcutsEnabled() const;
    // The window's shortcuts (UiShortcuts registers them).
    void setShortcutManager(app::ShortcutManager* manager);
    // The shortcuts that work now, by upstream's categories:
    // [{category, shortcuts: [{title, keys}]}].
    Q_INVOKABLE QVariantList activeShortcuts() const;

    // The built-in simulated boards (the connection popup's last entries).
    Q_INVOKABLE void connectSimulator(bool grblHal = false);
    Q_INVOKABLE void disconnectMachine();
    // A notification from the UI ("info", "success", "warning", "error"):
    // the toasts and the bell's list (Machine notices arrive the same way).
    Q_INVOKABLE void notify(const QString& text, const QString& type = QStringLiteral("info"));
    // Opening or closing the bell's list reads everything; Clear all.
    Q_INVOKABLE void readAllNotifications();
    Q_INVOKABLE void clearNotifications();
    Q_INVOKABLE void showHelper(const QString& title, const QString& text, const QString& link = {});
    Q_INVOKABLE void closeHelper();
    // The Maintenance Alert's Reset Timers.
    Q_INVOKABLE void resetMaintenanceTimers(const QVariantList& ids);
    // A tool of the Tools page ("rotarySurfacing", "surfacing", ...): the
    // page, opened on it.
    Q_INVOKABLE void openTool(const QString& name) { Q_EMIT toolRequested(name); }
    // A page of the rail ("carve", "stats", "tools", "config").
    Q_INVOKABLE void openPage(const QString& name) { Q_EMIT pageRequested(name); }

Q_SIGNALS:
    void notification(const QString& text, const QString& type);
    // A pop-up for `duration` ms (workspace.toastDuration: -1 until closed).
    void toast(const QString& text, const QString& type, int duration);
    void notificationsChanged();
    void helperChanged();
    // The alerts at a job's end (workspace/Alerts), each as the settings
    // allow: the Job End summary, and the maintenance tasks now due
    // ({id, name, description}).
    void jobEndSummary(bool completed, const QString& time, const QStringList& errors);
    void toolRequested(const QString& name);
    void pageRequested(const QString& name);
    // A keyboard shortcut for a screen to act on (UiShortcuts).
    void shortcutTriggered(const QString& id);
    // The connection closed while a job ran, around sender line `line`.
    void jobInterrupted(qint64 line);
    void maintenanceDue(const QVariantList& tasks);
    void connectionChanged();
    void stateChanged();
    void programChanged();
    void appSettingsChanged();

private:
    app::Machine& machine_;
    app::Jogger* jogger_;                      // a child
    app::NotificationCenter* notifications_;   // a child
    QPointer<app::ShortcutManager> shortcuts_;  // the window's
    bool helperVisible_ = false;
    QString helperTitle_;
    QString helperText_;
    QString helperLink_;
};

}  // namespace gs::ui
