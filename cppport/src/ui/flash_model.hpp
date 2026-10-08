#pragma once

// Config > Flash (features/Config/components/FlashDialog, FlashingProgress)
// for QML: the port, the controller type and the firmware file to pick, and
// a flash log with a progress bar. The port cannot flash a real board (that
// takes avrdude, DFU and UF2 flashers); the built-in simulators take the
// whole flow instead: the machine is disconnected, the flash runs with its
// progress and messages, and ends asking to reconnect, as upstream does.

#include "ui_model_base.hpp"

#include <QBasicTimer>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gs::ui {

class FlashModel : public UiModelBase {
    Q_OBJECT
    QML_ELEMENT

    // The ports that can be flashed: the simulators, while they are offered.
    Q_PROPERTY(QStringList ports READ ports NOTIFY portsChanged)
    Q_PROPERTY(bool available READ available NOTIFY portsChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int total READ total CONSTANT)
    // Newest first: {time, type (Info, Success, Warning, Error), content}.
    Q_PROPERTY(QVariantList log READ log NOTIFY logChanged)
    // Milliseconds per percent of the simulated flash (tests shorten it).
    Q_PROPERTY(int tickMs MEMBER tickMs_)

public:
    enum State { Idle, Flashing, Complete, Error };
    Q_ENUM(State)

    explicit FlashModel(QObject* parent = nullptr);

    QStringList ports() const;
    bool available() const;
    State state() const noexcept { return state_; }
    int progress() const noexcept { return progress_; }
    int total() const noexcept { return kTotal; }
    QVariantList log() const { return log_; }

    // Grbl needs only a port; grblHAL a firmware file as well (.hex or .uf2).
    Q_INVOKABLE bool canStart(const QString& port, const QString& controllerType, const QUrl& file) const;
    // "" once the flash is under way, else why not.
    Q_INVOKABLE QString start(const QString& port, const QString& controllerType, const QUrl& file);
    // Back to the form (the dialog opens again).
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void portsChanged();
    void stateChanged();
    void progressChanged();
    void logChanged();

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    static constexpr int kTotal = 100;
    void add(const QString& type, const QString& content);
    void setState(State state);

    int tickMs_ = 60;
    State state_ = Idle;
    int progress_ = 0;
    bool hal_ = false;
    QString file_;
    QVariantList log_;
    QBasicTimer timer_;
};

}  // namespace gs::ui
