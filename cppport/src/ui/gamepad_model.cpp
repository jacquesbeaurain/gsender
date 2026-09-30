#include "gamepad_model.hpp"

#include "backend.hpp"
#include "gamepad_service.hpp"
#include "machine.hpp"
#include "shortcuts.hpp"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QUrl>

#include <boost/json.hpp>

#include <algorithm>
#include <cmath>

namespace gs::ui {
namespace {

QString localPath(const QString& file) {
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

QString q(const std::string& s) {
    return QString::fromStdString(s);
}

gamepad::StickOptions& stickOf(gamepad::JoystickOptions& options, const QString& stick) {
    return stick == QLatin1String("stick2") ? options.stick2 : options.stick1;
}

gamepad::StickAction* directionOf(gamepad::StickOptions& stick, const QString& direction) {
    if (direction == QLatin1String("horizontal")) {
        return &stick.horizontal;
    }
    if (direction == QLatin1String("vertical")) {
        return &stick.vertical;
    }
    if (direction == QLatin1String("mpgMode")) {
        return &stick.mpgMode;
    }
    return nullptr;
}

QVariantMap actionMap(const gamepad::StickAction& a) {
    return {{"primaryAction", q(a.primaryAction)},
            {"secondaryAction", q(a.secondaryAction)},
            {"isReversed", a.isReversed}};
}

QVariantMap stickMap(const gamepad::StickOptions& s) {
    return {{"horizontal", actionMap(s.horizontal)},
            {"vertical", actionMap(s.vertical)},
            {"mpgMode", actionMap(s.mpgMode)}};
}

}  // namespace

GamepadModel::GamepadModel(QObject* parent) : UiModelBase(parent), service_(UiBackend::instance()->gamepad()) {
    service_.setCapturing(true);
    connect(&machine_, &app::Machine::appSettingsChanged, this, &GamepadModel::changed);
    connect(&machine_, &app::Machine::macrosChanged, this, &GamepadModel::changed);
    connect(&service_, &app::GamepadService::padsChanged, this, [this] {
        Q_EMIT changed();
        Q_EMIT padChanged();
    });
    connect(&service_, &app::GamepadService::buttonChanged, this, &GamepadModel::onButton);
    connect(&service_, &app::GamepadService::axisChanged, this, &GamepadModel::padChanged);
}

GamepadModel::~GamepadModel() {
    service_.setCapturing(false);
}

bool GamepadModel::available() const {
    return service_.available();
}

void GamepadModel::onButton(int index, int /*button*/, bool isPressed) {
    Q_EMIT padChanged();
    if (!isPressed) {
        return;
    }
    // ProfileModal's listener: the pad pressed, and whether it has a profile.
    const std::optional<gamepad::PadState>& pad = service_.listener().pad(index);
    if (!pad) {
        return;
    }
    detectedId_ = q(pad->id);
    detectedState_ = gamepad::findProfile(machine_.settings().gamepadProfiles, pad->id) ? QStringLiteral("exists")
                                                                                          : QStringLiteral("available");
    Q_EMIT detectChanged();
}

QVariantList GamepadModel::profiles() const {
    QVariantList out;
    const auto& profiles = machine_.settings().gamepadProfiles;
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        out.append(QVariantMap{{"index", static_cast<int>(i)},
                               {"name", q(profiles[i].name)},
                               {"connected", service_.padFor(profiles[i].ids).has_value()}});
    }
    return out;
}

const gamepad::Profile* GamepadModel::profile() const {
    const auto& profiles = machine_.settings().gamepadProfiles;
    return current_ >= 0 && current_ < static_cast<int>(profiles.size()) ? &profiles[static_cast<std::size_t>(current_)]
                                                                         : nullptr;
}

void GamepadModel::setCurrent(int index) {
    const int count = static_cast<int>(machine_.settings().gamepadProfiles.size());
    current_ = index >= 0 && index < count ? index : -1;
    Q_EMIT changed();
    Q_EMIT padChanged();
}

QString GamepadModel::name() const {
    const gamepad::Profile* p = profile();
    return p ? q(p->name) : QString();
}

bool GamepadModel::padConnected() const {
    const gamepad::Profile* p = profile();
    return p && service_.padFor(p->ids).has_value();
}

QVariantList GamepadModel::buttons() const {
    const gamepad::Profile* p = profile();
    if (!p) {
        return {};
    }
    std::vector<gamepad::ButtonMapping> rows = p->buttons;
    // The 2nd-action and lockout buttons first.
    std::stable_sort(rows.begin(), rows.end(), [p](const gamepad::ButtonMapping& a, const gamepad::ButtonMapping& b) {
        const auto special = [p](const gamepad::ButtonMapping& m) {
            return m.value == p->modifier || m.value == p->lockout;
        };
        return special(a) && !special(b);
    });
    QVariantList out;
    for (const gamepad::ButtonMapping& b : rows) {
        const QString role = b.value == p->lockout    ? QStringLiteral("lockout")
                             : b.value == p->modifier ? QStringLiteral("modifier")
                                                      : QString();
        out.append(QVariantMap{{"value", b.value},
                               {"label", q(b.label)},
                               {"primary", q(b.primaryAction)},
                               {"primaryTitle", b.primaryAction.empty() ? QString() : actionTitle(q(b.primaryAction))},
                               {"secondary", q(b.secondaryAction)},
                               {"secondaryTitle",
                                b.secondaryAction.empty() ? QString() : actionTitle(q(b.secondaryAction))},
                               {"role", role}});
    }
    return out;
}

QVariantList GamepadModel::pressed() const {
    const gamepad::Profile* p = profile();
    const std::optional<gamepad::PadState> pad = p ? service_.padFor(p->ids) : std::nullopt;
    QVariantList out;
    if (pad) {
        for (const bool down : pad->buttons) {
            out.append(down);
        }
    }
    return out;
}

bool GamepadModel::modifierHeld() const {
    const gamepad::Profile* p = profile();
    const std::optional<gamepad::PadState> pad = p ? service_.padFor(p->ids) : std::nullopt;
    return pad && gamepad::isHeld(p->modifier, pad->buttons);
}

QVariantList GamepadModel::axes() const {
    const gamepad::Profile* p = profile();
    const std::optional<gamepad::PadState> pad = p ? service_.padFor(p->ids) : std::nullopt;
    QVariantList out;
    if (pad) {
        for (const double value : pad->axes) {
            out.append(std::abs(value) < 0.4 ? 0.0 : value);
        }
    }
    return out;
}

QVariantMap GamepadModel::joystick() const {
    const gamepad::Profile* p = profile();
    const gamepad::JoystickOptions options = p ? p->joystickOptions : gamepad::JoystickOptions{};
    return {{"stick1", stickMap(options.stick1)},
            {"stick2", stickMap(options.stick2)},
            {"zeroThreshold", options.zeroThreshold},
            {"movementDistanceOverride", options.movementDistanceOverride},
            {"fixedSpeedMode", options.fixedSpeedMode}};
}

QVariantList GamepadModel::actionCategories() const {
    // SetShortcut's categoryOrder.
    static const QStringList order{"Jogging",       "Location", "Macros",    "Probing",  "Spindle/Laser", "Coolant",
                                   "Carving",       "Overrides", "General",  "Toolbar",  "Visualizer"};
    const std::vector<app::ShortcutAction> actions = app::shortcutActions(machine_);
    QStringList categories;
    for (const app::ShortcutAction& action : actions) {
        if (!categories.contains(action.category)) {
            categories.append(action.category);
        }
    }
    std::stable_sort(categories.begin(), categories.end(), [](const QString& a, const QString& b) {
        const auto rank = [](const QString& c) {
            const auto i = order.indexOf(c);
            return i < 0 ? order.size() : i;
        };
        return rank(a) < rank(b);
    });
    QVariantList out;
    for (const QString& category : categories) {
        QVariantList list;
        for (const app::ShortcutAction& action : actions) {
            if (action.category == category && action.id != QLatin1String("STOP_CONT_JOG")) {
                list.append(QVariantMap{{"id", action.id}, {"title", action.title}});
            }
        }
        if (!list.isEmpty()) {
            out.append(QVariantMap{{"category", category}, {"actions", list}});
        }
    }
    return out;
}

QString GamepadModel::actionTitle(const QString& id) const {
    for (const app::ShortcutAction& action : app::shortcutActions(machine_)) {
        if (action.id == id) {
            return action.title;
        }
    }
    return id;
}

void GamepadModel::save(std::vector<gamepad::Profile> profiles) {
    app::AppSettings settings = machine_.settings();
    settings.gamepadProfiles = std::move(profiles);
    machine_.setSettings(settings);
}

template <typename Edit>
void GamepadModel::editProfile(Edit&& edit) {
    std::vector<gamepad::Profile> profiles = machine_.settings().gamepadProfiles;
    if (current_ < 0 || current_ >= static_cast<int>(profiles.size())) {
        return;
    }
    edit(profiles[static_cast<std::size_t>(current_)]);
    save(std::move(profiles));
}

void GamepadModel::resetDetection() {
    detectedId_.clear();
    detectedState_.clear();
    Q_EMIT detectChanged();
}

bool GamepadModel::addProfile(const QString& name) {
    if (detectedState_ != QLatin1String("available")) {
        return false;
    }
    std::optional<gamepad::PadState> pad;
    for (int i = 0; i < gamepad::kSlots && !pad; ++i) {
        if (const auto& p = service_.listener().pad(i); p && q(p->id) == detectedId_) {
            pad = p;
        }
    }
    if (!pad) {
        return false;
    }
    gamepad::Profile profile;
    profile.ids = {pad->id};
    profile.name = name.trimmed().isEmpty() ? pad->id : name.trimmed().toStdString();
    profile.mapping = pad->standard ? "standard" : "";
    profile.buttons = gamepad::defaultButtons(static_cast<int>(pad->buttons.size()), pad->standard);
    std::vector<gamepad::Profile> profiles = machine_.settings().gamepadProfiles;
    profiles.push_back(std::move(profile));
    save(std::move(profiles));
    detectedState_ = QStringLiteral("exists");
    Q_EMIT detectChanged();
    return true;
}

void GamepadModel::removeProfile(int index) {
    std::vector<gamepad::Profile> profiles = machine_.settings().gamepadProfiles;
    if (index < 0 || index >= static_cast<int>(profiles.size())) {
        return;
    }
    profiles.erase(profiles.begin() + index);
    if (current_ == index) {
        current_ = -1;
    } else if (current_ > index) {
        --current_;
    }
    save(std::move(profiles));
}

void GamepadModel::rename(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == this->name()) {
        return;
    }
    editProfile([&](gamepad::Profile& p) { p.name = trimmed.toStdString(); });
}

void GamepadModel::setAction(int button, bool secondary, const QString& id) {
    editProfile([&](gamepad::Profile& p) {
        if (gamepad::ButtonMapping* b = p.button(button)) {
            (secondary ? b->secondaryAction : b->primaryAction) = id.toStdString();
        }
    });
}

void GamepadModel::clearAction(int button, bool secondary) {
    setAction(button, secondary, QString());
}

void GamepadModel::setLockout(int button, bool on) {
    editProfile([&](gamepad::Profile& p) {
        p.lockout = on ? std::optional<int>(button) : std::nullopt;
        if (on && p.modifier == button) {
            p.modifier.reset();
        }
    });
}

void GamepadModel::setModifier(int button, bool on) {
    editProfile([&](gamepad::Profile& p) {
        p.modifier = on ? std::optional<int>(button) : std::nullopt;
        if (on && p.lockout == button) {
            p.lockout.reset();
        }
    });
}

void GamepadModel::setLabel(int button, const QString& label) {
    editProfile([&](gamepad::Profile& p) {
        if (gamepad::ButtonMapping* b = p.button(button)) {
            b->label = label.toStdString();
        }
    });
}

void GamepadModel::setStick(const QString& stick, const QString& direction, const QString& field,
                            const QVariant& value) {
    editProfile([&](gamepad::Profile& p) {
        gamepad::StickAction* action = directionOf(stickOf(p.joystickOptions, stick), direction);
        if (!action) {
            return;
        }
        if (field == QLatin1String("isReversed")) {
            action->isReversed = value.toBool();
        } else if (field == QLatin1String("primaryAction")) {
            action->primaryAction = value.toString().toLower().toStdString();
        } else if (field == QLatin1String("secondaryAction")) {
            action->secondaryAction = value.toString().toLower().toStdString();
        }
    });
}

void GamepadModel::setOption(const QString& key, const QVariant& value) {
    editProfile([&](gamepad::Profile& p) {
        gamepad::JoystickOptions& o = p.joystickOptions;
        if (key == QLatin1String("zeroThreshold")) {
            o.zeroThreshold = std::clamp(value.toDouble(), 0.0, 99.0);
        } else if (key == QLatin1String("movementDistanceOverride")) {
            // ControlledInput's onChange ignores values outside 0.001-99999.
            const double v = value.toDouble();
            if (v >= 0.001 && v <= 99999) {
                o.movementDistanceOverride = v;
            }
        } else if (key == QLatin1String("fixedSpeedMode")) {
            o.fixedSpeedMode = value.toBool();
        }
    });
}

QVariantMap GamepadModel::importFile(const QString& file) {
    const gamepad::Profile* p = profile();
    QFile in(localPath(file));
    if (!p || !in.open(QIODevice::ReadOnly)) {
        return {{"ok", false}, {"message", tr("Failed to import gamepad profile. Please check the file format.")}};
    }
    const QByteArray bytes = in.readAll();
    boost::system::error_code error;
    const boost::json::value json =
        boost::json::parse(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), error);
    const std::optional<gamepad::Profile> merged = error ? std::nullopt : gamepad::importProfile(*p, json);
    if (!merged) {
        return {{"ok", false}, {"message", tr("Failed to import gamepad profile. Please check the file format.")}};
    }
    editProfile([&](gamepad::Profile& target) { target = *merged; });
    return {{"ok", true}, {"message", tr("Gamepad profile imported successfully!")}};
}

QVariantMap GamepadModel::exportFile(const QString& file) {
    const gamepad::Profile* p = profile();
    if (!p) {
        return {{"ok", false}, {"message", tr("Failed to export gamepad profile.")}};
    }
    QFile out(localPath(file));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {{"ok", false}, {"message", tr("Failed to export gamepad profile.")}};
    }
    const QString date = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const std::string text = boost::json::serialize(gamepad::exportProfile(*p, date.toStdString()));
    out.write(text.data(), static_cast<qint64>(text.size()));
    return {{"ok", true}, {"message", tr("Gamepad profile exported successfully!")}};
}

QString GamepadModel::exportName() const {
    // gsender-gamepad-<name, spaces as dashes, lower case>-<date>.json
    QString name = this->name().toLower();
    name.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("-"));
    return QStringLiteral("gsender-gamepad-%1-%2.json").arg(name, QDate::currentDate().toString(Qt::ISODate));
}

}  // namespace gs::ui
