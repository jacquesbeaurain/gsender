#include "remote_models.hpp"

#include "backend.hpp"
#include "config_model.hpp"
#include "connection_model.hpp"
#include "dro_model.hpp"
#include "file_model.hpp"
#include "job_model.hpp"
#include "macros_model.hpp"
#include "probe_model.hpp"
#include "rotary_model.hpp"
#include "spindle_model.hpp"
#include "status_model.hpp"

#include "remote_service.hpp"

#include <QVariantList>

namespace gs::ui {
namespace {

// The notification list (features/NotificationsArea) for the Info page: the
// application's own, read and cleared as the desktop does.
class RemoteNotificationsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY changed)
    Q_PROPERTY(int unreadErrors READ unreadErrors NOTIFY changed)

public:
    explicit RemoteNotificationsModel(QObject* parent) : QObject(parent) {
        connect(UiBackend::instance(), &UiBackend::notificationsChanged, this, &RemoteNotificationsModel::changed);
    }

    QVariantList notifications() const { return UiBackend::instance()->notifications(); }
    int unreadErrors() const { return UiBackend::instance()->unreadErrors(); }

    Q_INVOKABLE void readAll() { UiBackend::instance()->readAllNotifications(); }
    Q_INVOKABLE void clear() { UiBackend::instance()->clearNotifications(); }

Q_SIGNALS:
    void changed();
};

}  // namespace

void bindRemoteModels(app::RemoteService& remote, QObject* owner) {
    // Upstream's Tools route: Probe, Macros, Spindle, Coolant, Rotary.
    remote.bindModel("macros", {[owner] { return new MacrosModel(owner); }, {"run"}, {}});
    remote.bindModel("spindle",
                     {[owner] { return new SpindleModel(owner); },
                      {"startClockwise", "startCounterClockwise", "stop", "toggleMode", "setSpeed", "setPower",
                       "setDuration", "selectSpindle", "applyNow"},
                      {}});
    remote.bindModel("coolant", {[owner] { return new CoolantModel(owner); }, {"mist", "flood", "off"}, {}});
    remote.bindModel("probe",
                     {[owner] { return new ProbeModel(owner); },
                      {"selectCommand", "stepCommand", "setPlateType", "selectTool", "addTool", "removeTool",
                       "nextCorner", "beginRun", "confirmCircuit", "start"},
                      {}});
    remote.bindModel("rotary",
                     {[owner] { return new RotaryModel(owner); }, {"enableConfirmation", "setRotaryMode", "runProbe"}, {}});
    // The Config route. What names a file on the computer (import, export,
    // folders) means nothing from a phone and is left out.
    remote.bindModel("config",
                     {[owner] { return new ConfigModel(owner); },
                      {"sectionRow", "valueOf", "setValue", "setNumber", "setPart", "resetValue", "setEeprom",
                       "toggleEepromBit", "resetEeprom", "apply", "revert", "useCurrentPosition", "goToLocation",
                       "sendTest", "jogAxis", "restoreDefaultSettings", "restoreFirmwareDefaults", "reloadFirmware"},
                      {"search", "onlyModified", "profileId"}});

    // The top bar's connection, the units badge, the job and its file, the
    // machine's information and the notifications.
    remote.bindModel("connection",
                     {[owner] { return new ConnectionModel(owner); },
                      {"refresh", "connectTo", "connectEthernet", "disconnectMachine"},
                      {}});
    remote.bindModel("dro", {[owner] { return new DroModel(owner); }, {"toggleUnits"}, {}});
    // A file is chosen on the phone and sent over (loadProgram); one on the
    // computer is not for the phone to name.
    remote.bindModel("file", {[owner] { return new FileModel(owner); }, {"reload", "close"}, {}});
    remote.bindModel("job",
                     {[owner] { return new JobModel(owner); },
                      {"outline", "startFromLine", "startLine", "safeHeight", "setFeedOverride", "setSpindleOverride"},
                      {}});
    remote.bindModel("status",
                     {[owner] { return new StatusModel(owner); },
                      {"clickAlarmButton", "clickLock", "resolveHomingFailure", "homingFailureText", "setStepperLock"},
                      {}});
    remote.bindModel("notifications",
                     {[owner] { return new RemoteNotificationsModel(owner); }, {"readAll", "clear"}, {}});
}

}  // namespace gs::ui

#include "remote_models.moc"
