#pragma once

// What the QML UI binds to: the application's Machine seen as properties,
// signals and invokable actions. QML stays declarative; decisions stay in
// C++ (here, the core and the app's services), where tests reach them.
//
// One instance per UI, registered before the QML loads (setInstance) and
// served to QML as the `Backend` singleton.

#include <QColor>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

namespace gs::app {
class Machine;
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

public:
    explicit UiBackend(app::Machine& machine, QObject* parent = nullptr);

    // The instance QML's `Backend` is (set before loading the QML).
    static void setInstance(UiBackend* backend);
    static UiBackend* instance();
    static UiBackend* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    app::Machine& machine() noexcept { return machine_; }

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
    void setDarkMode(bool dark);

    Q_INVOKABLE void connectSimulator(bool grblHal = false);
    Q_INVOKABLE void disconnectMachine();

Q_SIGNALS:
    void connectionChanged();
    void stateChanged();
    void programChanged();
    void appSettingsChanged();

private:
    app::Machine& machine_;
};

}  // namespace gs::ui
