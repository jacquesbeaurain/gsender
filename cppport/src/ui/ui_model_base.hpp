#pragma once

#include <QObject>
#include <QString>

namespace gs::app {
class Machine;
}

namespace gs::ui {

// Base class for QML view models, providing common access to Machine state,
// connection status, unit settings, coordinate formatting, and signal routing.
class UiModelBase : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool metric READ metric NOTIFY changed)
    Q_PROPERTY(QString units READ units NOTIFY changed)
    Q_PROPERTY(bool canClick READ canClick NOTIFY changed)

public:
    explicit UiModelBase(QObject* parent = nullptr);
    explicit UiModelBase(app::Machine& machine, QObject* parent = nullptr);
    ~UiModelBase() override = default;

    app::Machine& machine() noexcept { return machine_; }
    const app::Machine& machine() const noexcept { return machine_; }

    bool connected() const;
    bool metric() const;
    QString units() const;
    bool canClick() const;

    Q_INVOKABLE QString positionText(double mm) const;
    Q_INVOKABLE QString positionText(double mm, char axis) const;

Q_SIGNALS:
    void changed();

protected:
    // Subscribes `changed()` to standard machine status, connection, settings, and workflow signals.
    void connectMachineSignals(bool includeSettings = true, bool includeWorkflow = true);

    app::Machine& machine_;
};

}  // namespace gs::ui
