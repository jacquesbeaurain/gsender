#include "backend.hpp"

#include "jogger.hpp"
#include "machine.hpp"
#include "notification_center.hpp"
#include "shortcuts.hpp"

#include "gs/controller/actions.hpp"
#include "gs/controller/controller.hpp"
#include "gs/util/datetime.hpp"

#include <QDateTime>
#include <QJSEngine>
#include <QPointer>
#include <QVariantMap>

namespace gs::ui {
namespace {

QPointer<UiBackend> gInstance;

QString typeName(app::NotificationType type) {
    switch (type) {
        case app::NotificationType::Success: return QStringLiteral("success");
        case app::NotificationType::Error: return QStringLiteral("error");
        case app::NotificationType::Warning: return QStringLiteral("warning");
        case app::NotificationType::Info: break;
    }
    return QStringLiteral("info");
}

app::NotificationType typeOf(const QString& name) {
    return name == "success" ? app::NotificationType::Success
         : name == "error"   ? app::NotificationType::Error
         : name == "warning" ? app::NotificationType::Warning
                             : app::NotificationType::Info;
}

}  // namespace

UiBackend::UiBackend(app::Machine& machine, QObject* parent)
    : QObject(parent),
      machine_(machine),
      jogger_(new app::Jogger(machine, this)),
      notifications_(new app::NotificationCenter(this)) {
    // Every notification pops up for workspace.toastDuration and is kept.
    connect(notifications_, &app::NotificationCenter::added, this, [this](const app::Notification& n) {
        Q_EMIT toast(n.message, typeName(n.type), machine_.settings().toastDuration);
    });
    connect(notifications_, &app::NotificationCenter::changed, this, &UiBackend::notificationsChanged);
    // The machine's notices and errors (toast.error("Error 20: ...")): the
    // first line pops up; the console has it all.
    const auto announce = [this](const QString& type) {
        return [this, type](const QString& text) {
            machine_.consoleLog().write(text, app::ConsoleType::System);
            notify(text, type);
        };
    };
    connect(&machine_, &app::Machine::notice, this, announce(QStringLiteral("info")));
    connect(&machine_, &app::Machine::successNotice, this, announce(QStringLiteral("success")));
    const auto error = [this](const QString& title, const QString& detail) {
        machine_.consoleLog().write(title + ": " + detail, app::ConsoleType::Error);
        notify(title + ": " + detail.section('\n', 0, 0), QStringLiteral("error"));
    };
    connect(&machine_, &app::Machine::errorReported, this, error);
    connect(&machine_, &app::Machine::connectionFailed, this,
            [error](const QString& reason) { error(tr("Connection"), reason); });
    // AccessoryConnectivityToast: its own pop-up, not kept in the list.
    connect(&machine_, &app::Machine::accessoryConnectivityChanged, this,
            [this](const QString& accessory, bool connected) {
                Q_EMIT toast(connected ? tr("%1 connected - Ready to use").arg(accessory)
                                       : tr("%1 disconnected - Connection lost").arg(accessory),
                             connected ? QStringLiteral("success") : QStringLiteral("warning"), 0);
            });
    // "Warn if bad file" and "Warn on bad line": the Helper panel.
    connect(&machine_, &app::Machine::invalidLinesFound, this, [this](int count, const QStringList& sample) {
        QString html = "<p>" + tr("Detected %1 invalid lines on file load. Your job may not run correctly.").arg(count) +
                       "</p><p>" + tr("Sample invalid lines found include:") + "</p><p>";
        for (const QString& line : sample) {
            html += "-<b> " + line.toHtmlEscaped() + "</b><br>";
        }
        showHelper(tr("Invalid Lines Detected"), html + "</p>");
    });
    connect(&machine_, &app::Machine::lineWarning, this, [this](const QString& code, const QString& line) {
        showHelper(tr("Invalid Line"), "<p>" +
                                           tr("The following line caused an <b>error %1</b>: <i>'%2'</i>")
                                               .arg(code, line.toHtmlEscaped()) +
                                           "</p><p>" + tr("Press Start to resume the job.") + "</p>");
    });
    // The alerts at a job's end, as the settings allow.
    connect(&machine_, &app::Machine::jobEnded, this,
            [this](bool completed, double durationMs, const QStringList& errors) {
                const app::AppSettings& settings = machine_.settings();
                if (settings.jobEndModal) {
                    Q_EMIT jobEndSummary(completed, QString::fromStdString(util::millisecondsToTimeStamp(durationMs)),
                                         errors);
                }
                if (settings.maintenanceNotifications) {
                    QVariantList due;
                    for (const config::MaintenanceTask& task : machine_.dueMaintenanceTasks()) {
                        due.append(QVariantMap{{"id", task.id},
                                               {"name", QString::fromStdString(task.name)},
                                               {"description", QString::fromStdString(task.description)}});
                    }
                    if (!due.isEmpty()) {
                        Q_EMIT maintenanceDue(due);
                    }
                }
            });
    connect(&machine_, &app::Machine::jobInterrupted, this, &UiBackend::jobInterrupted);
    connect(&machine_, &app::Machine::connectionChanged, this, &UiBackend::connectionChanged);
    connect(&machine_, &app::Machine::connectionChanged, this, &UiBackend::stateChanged);
    connect(&machine_, &app::Machine::stateChanged, this, &UiBackend::stateChanged);
    connect(&machine_, &app::Machine::programChanged, this, &UiBackend::programChanged);
    connect(&machine_, &app::Machine::appSettingsChanged, this, &UiBackend::appSettingsChanged);
}

void UiBackend::setInstance(UiBackend* backend) {
    gInstance = backend;
}

UiBackend* UiBackend::instance() {
    return gInstance.data();
}

UiBackend* UiBackend::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT(gInstance);
    // Owned by the application, not the engine.
    QJSEngine::setObjectOwnership(gInstance.data(), QJSEngine::CppOwnership);
    return gInstance.data();
}

bool UiBackend::connected() const {
    return machine_.isConnected();
}

bool UiBackend::connecting() const {
    return machine_.isConnecting();
}

QString UiBackend::portLabel() const {
    const QString port = machine_.port();
    if (app::Machine::isSimulatorPort(port)) {
        return tr("Simulator");
    }
    return port;
}

QString UiBackend::firmwareLabel() const {
    controller::Controller* c = machine_.controller();
    if (!c || !machine_.isConnected()) {
        return {};
    }
    return c->isGrblHal() ? QStringLiteral("grblHAL") : QStringLiteral("Grbl");
}

QString UiBackend::connectionKind() const {
    const QString port = machine_.port();
    if (app::Machine::isSimulatorPort(port)) {
        return QStringLiteral("simulator");
    }
    // An address is the Ethernet board (ConnectionStateIndicator's BsEthernet).
    const bool network = !port.isEmpty() && port.front().isDigit() && port.contains('.');
    return network ? QStringLiteral("ethernet") : QStringLiteral("usb");
}

QString UiBackend::activeState() const {
    controller::Controller* c = machine_.controller();
    return c && machine_.isConnected() ? QString::fromStdString(c->state().status.activeState) : QString();
}

QString UiBackend::stateText() const {
    const QString state = activeState();
    if (state.isEmpty()) {
        return tr("Disconnected");
    }
    return QString::fromStdString(controller::statusLabel(state.toStdString()));
}

QString UiBackend::alarmCode() const {
    controller::Controller* c = machine_.controller();
    return c && activeState() == "Alarm" ? QString::fromStdString(c->state().status.alarmCode) : QString();
}

bool UiBackend::hasProgram() const {
    return machine_.hasProgram();
}

QString UiBackend::programName() const {
    return machine_.programName();
}

bool UiBackend::spindleFunctions() const {
    return machine_.settings().spindleFunctions;
}

bool UiBackend::coolantFunctions() const {
    return machine_.settings().coolantFunctions;
}

bool UiBackend::rotaryTab() const {
    return machine_.settings().rotary.showControls;
}

bool UiBackend::keyboardMap() const {
    return machine_.settings().accessibility.showKeyboardMap;
}

void UiBackend::setKeyboardMap(bool shown) {
    if (shown != keyboardMap()) {
        app::AppSettings settings = machine_.settings();
        settings.accessibility.showKeyboardMap = shown;
        machine_.setSettings(settings);
    }
}

bool UiBackend::shortcutsEnabled() const {
    return machine_.settings().shortcutsEnabled;
}

void UiBackend::setShortcutManager(app::ShortcutManager* manager) {
    shortcuts_ = manager;
}

QVariantList UiBackend::activeShortcuts() const {
    QVariantList list;
    if (!shortcuts_) {
        return list;
    }
    // Upstream's order of the categories.
    static const char* const kCategories[] = {"General", "Location",  "Jogging",    "Spindle/Laser",
                                              "Coolant", "Carving",   "Toolbar",    "Visualizer",
                                              "Macros",  "Overrides", "Probing"};
    const std::vector<app::ShortcutManager::ActiveShortcut> active = shortcuts_->activeShortcuts();
    for (const char* category : kCategories) {
        QVariantList shortcuts;
        for (const app::ShortcutManager::ActiveShortcut& shortcut : active) {
            if (shortcut.category == QLatin1String(category)) {
                shortcuts.append(QVariantMap{{"title", shortcut.title}, {"keys", shortcut.keys}});
            }
        }
        if (!shortcuts.isEmpty()) {
            list.append(QVariantMap{{"category", QString::fromLatin1(category)}, {"shortcuts", shortcuts}});
        }
    }
    return list;
}

bool UiBackend::darkMode() const {
    return machine_.settings().darkMode;
}

void UiBackend::setDarkMode(bool dark) {
    if (dark != darkMode()) {
        app::AppSettings settings = machine_.settings();
        settings.darkMode = dark;
        machine_.setSettings(settings);
    }
}

void UiBackend::connectSimulator(bool grblHal) {
    machine_.connectTo(grblHal ? app::Machine::kSimulatorHalPort : app::Machine::kSimulatorPort);
}

void UiBackend::disconnectMachine() {
    machine_.disconnectFromMachine();
}

void UiBackend::notify(const QString& text, const QString& type) {
    Q_EMIT notification(text, type);
    notifications_->add(text, typeOf(type));
}

QVariantList UiBackend::notifications() const {
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    QVariantList list;
    const auto& all = notifications_->list();
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        list.append(QVariantMap{{"message", it->message},
                                {"type", typeName(it->type)},
                                {"read", it->read},
                                {"ago", QString::fromStdString(util::timeAgo(it->time, now))}});
    }
    return list;
}

int UiBackend::unreadErrors() const {
    return notifications_->unreadErrors();
}

void UiBackend::readAllNotifications() {
    notifications_->readAll();
}

void UiBackend::clearNotifications() {
    notifications_->clear();
}

void UiBackend::showHelper(const QString& title, const QString& text, const QString& link) {
    helperTitle_ = title;
    helperText_ = text;
    helperLink_ = link;
    helperVisible_ = true;
    Q_EMIT helperChanged();
}

void UiBackend::closeHelper() {
    if (helperVisible_) {
        helperVisible_ = false;
        Q_EMIT helperChanged();
    }
}

void UiBackend::resetMaintenanceTimers(const QVariantList& ids) {
    std::vector<int> list;
    for (const QVariant& id : ids) {
        list.push_back(id.toInt());
    }
    machine_.resetMaintenanceTimers(list);
}

}  // namespace gs::ui
