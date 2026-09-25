#include "probe_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"
#include "gs/probe/probing.hpp"
#include "gs/util/jsnumber.hpp"

#include <algorithm>
#include <cmath>

namespace gs::ui {
namespace {

constexpr probe::PlateType kPlates[] = {probe::PlateType::StandardBlock, probe::PlateType::AutoZero,
                                        probe::PlateType::ZProbe, probe::PlateType::Probe3D,
                                        probe::PlateType::BitZero};

bool autoPlate(probe::PlateType plate) {
    return plate == probe::PlateType::AutoZero || plate == probe::PlateType::BitZero;
}

QString number(double value) {
    return QString::fromStdString(js::numberToString(value));
}

}  // namespace

ProbeModel::ProbeModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    connect(&machine_, &app::Machine::appSettingsChanged, this, &ProbeModel::settingsChanged);
    // A touch once seen keeps the check passed (setProbeConnectivity).
    connect(&machine_, &app::Machine::stateChanged, this, [this] { checked_ = checked_ || machine_.probeTriggered(); });
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::connectionChanged, &app::Machine::workflowChanged}) {
        connect(&machine_, signal, this, &ProbeModel::changed);
    }
    settingsChanged();
}

void ProbeModel::settingsChanged() {
    const int count = static_cast<int>(probe::probeCommands(machine_.settings().probe.plateType).size());
    selected_ = std::clamp(selected_, 0, std::max(0, count - 1));
    // The tool stays while the plate offers it, else the first choice.
    const QVariantList choices = tools();
    const bool offered = std::any_of(choices.begin(), choices.end(), [this](const QVariant& choice) {
        return choice.toMap().value("value").toString() == tool_;
    });
    if (!offered) {
        // As upstream starts: Auto on an AutoZero, else the first tool
        // (6.35 mm / 0.25 in unless changed).
        const app::AppSettings& settings = machine_.settings();
        if (autoPlate(settings.probe.plateType) || settings.probeTools.empty()) {
            tool_ = choices.isEmpty() ? QString() : choices.first().toMap().value("value").toString();
        } else {
            const probe::ToolDiameter& first = settings.probeTools.front();
            tool_ = number(settings.metric ? first.metric : first.imperial);
        }
    }
    Q_EMIT changed();
}

bool ProbeModel::canClick() const {
    controller::Controller* c = machine_.controller();
    return c && !c->workflow().isRunning() && c->state().status.activeState == "Idle";
}

bool ProbeModel::connected() const {
    return machine_.isConnected();
}

QString ProbeModel::plateType() const {
    return QString::fromUtf8(probe::plateTypeName(machine_.settings().probe.plateType).data());
}

QStringList ProbeModel::plateTypes() const {
    QStringList names;
    for (const probe::PlateType type : kPlates) {
        names << QString::fromUtf8(probe::plateTypeName(type).data());
    }
    return names;
}

bool ProbeModel::plateSwitcher() const {
    return machine_.settings().touchplateTypeSwitcher;
}

QVariantList ProbeModel::commands() const {
    QVariantList list;
    for (const probe::ProbeCommand& command : probe::probeCommands(machine_.settings().probe.plateType)) {
        const QString id = QString::fromStdString(command.id);
        list.append(QVariantMap{{"id", id}, {"label", id.section(' ', 0, 0)}});
    }
    return list;
}

QString ProbeModel::commandId() const {
    const auto commands = probe::probeCommands(machine_.settings().probe.plateType);
    return selected_ < static_cast<int>(commands.size()) ? QString::fromStdString(commands[selected_].id) : QString();
}

bool ProbeModel::needsTool() const {
    const auto commands = probe::probeCommands(machine_.settings().probe.plateType);
    return selected_ < static_cast<int>(commands.size()) && commands[selected_].needsTool;
}

QString ProbeModel::units() const {
    return machine_.settings().metric ? QStringLiteral("mm") : QStringLiteral("in");
}

QVariantList ProbeModel::tools() const {
    const app::AppSettings& settings = machine_.settings();
    QVariantList list;
    // Auto and Tip first, then the diameters smallest first (the options' sort).
    if (autoPlate(settings.probe.plateType)) {
        for (const probe::ProbeType type : {probe::ProbeType::Auto, probe::ProbeType::Tip}) {
            const QString name = QString::fromUtf8(probe::probeTypeName(type).data());
            list.append(QVariantMap{{"value", name}, {"label", name}, {"removable", false}});
        }
    }
    std::vector<double> diameters;
    for (const probe::ToolDiameter& tool : settings.probeTools) {
        const double d = settings.metric ? tool.metric : tool.imperial;
        if (d > 0 && std::find(diameters.begin(), diameters.end(), d) == diameters.end()) {
            diameters.push_back(d);
        }
    }
    std::sort(diameters.begin(), diameters.end());
    const bool removable = diameters.size() > 1;
    for (const double d : diameters) {
        list.append(QVariantMap{{"value", number(d)}, {"label", number(d) + " " + units()}, {"removable", removable}});
    }
    return list;
}

int ProbeModel::probeTypeIndex() const {
    if (autoPlate(machine_.settings().probe.plateType)) {
        if (const auto type = probe::probeTypeFromName(tool_.toStdString())) {
            return static_cast<int>(*type);
        }
    }
    return static_cast<int>(probe::ProbeType::Diameter);
}

double ProbeModel::toolDiameter() const {
    if (probeTypeIndex() != static_cast<int>(probe::ProbeType::Diameter)) {
        return 0;
    }
    bool ok = false;
    const double value = tool_.toDouble(&ok);
    return ok && value > 0 ? value : 0;
}

int ProbeModel::corner() const {
    return machine_.settings().probe.direction;
}

QString ProbeModel::cornerName() const {
    switch (corner()) {
        case probe::kTopLeft: return tr("Top left");
        case probe::kTopRight: return tr("Top right");
        case probe::kBottomRight: return tr("Bottom right");
        default: return tr("Bottom left");
    }
}

QString ProbeModel::image() const {
    const probe::PlateType plate = machine_.settings().probe.plateType;
    const QString id = commandId();
    QString name;
    if (autoPlate(plate)) {
        name = id == "Z Touch" ? "AutoZero-Z" : "AutoZero-Rem";
    } else if (plate == probe::PlateType::ZProbe) {
        name = "Probe-Z";
    } else {
        const QString prefix = plate == probe::PlateType::Probe3D ? "3D-" : "Block-";
        const QString axes = id.section(' ', 0, 0);
        name = prefix + (plate == probe::PlateType::Probe3D && axes == "Z" ? "XYZ" : axes);
    }
    return "qrc:/images/probe/" + name + ".gif";
}

bool ProbeModel::probe3D() const {
    return machine_.settings().probe.plateType == probe::PlateType::Probe3D;
}

bool ProbeModel::probeTriggered() const {
    return machine_.probeTriggered();
}

bool ProbeModel::simulated() const {
    return machine_.isSimulated();
}

void ProbeModel::selectCommand(int index) {
    const int count = static_cast<int>(probe::probeCommands(machine_.settings().probe.plateType).size());
    if (index >= 0 && index < count && index != selected_) {
        selected_ = index;
        Q_EMIT changed();
    }
}

void ProbeModel::stepCommand(int delta) {
    const int count = static_cast<int>(probe::probeCommands(machine_.settings().probe.plateType).size());
    if (count > 0) {
        selectCommand(((selected_ + delta) % count + count) % count);
    }
}

void ProbeModel::setPlateType(const QString& name) {
    const auto type = probe::plateTypeFromName(name.toStdString());
    if (type && *type != machine_.settings().probe.plateType) {
        app::AppSettings settings = machine_.settings();
        settings.probe.plateType = *type;
        machine_.setSettings(settings);  // back through settingsChanged()
    }
}

void ProbeModel::selectTool(const QString& value) {
    if (value != tool_) {
        tool_ = value;
        Q_EMIT changed();
    }
}

bool ProbeModel::addTool(const QString& text) {
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value) || value <= 0) {
        return false;
    }
    app::AppSettings settings = machine_.settings();
    const bool metric = settings.metric;
    const bool known = std::any_of(settings.probeTools.begin(), settings.probeTools.end(),
                                   [&](const probe::ToolDiameter& t) { return (metric ? t.metric : t.imperial) == value; });
    if (!known) {
        // The other unit to 3 decimals, as upstream.
        const double other = std::round((metric ? value / 25.4 : value * 25.4) * 1000) / 1000;
        settings.probeTools.push_back(metric ? probe::ToolDiameter{value, other} : probe::ToolDiameter{other, value});
        tool_ = number(value);
        machine_.setSettings(settings);
    } else {
        selectTool(number(value));
    }
    return true;
}

void ProbeModel::removeTool(const QString& value) {
    app::AppSettings settings = machine_.settings();
    const bool metric = settings.metric;
    const auto before = settings.probeTools.size();
    std::erase_if(settings.probeTools,
                  [&](const probe::ToolDiameter& t) { return number(metric ? t.metric : t.imperial) == value; });
    if (settings.probeTools.size() != before && !settings.probeTools.empty()) {
        machine_.setSettings(settings);
    }
}

void ProbeModel::nextCorner() {
    app::AppSettings settings = machine_.settings();
    settings.probe.direction = probe::nextCorner(settings.probe.direction);
    machine_.setSettings(settings);
}

void ProbeModel::beginRun() {
    checked_ = !machine_.settings().probe.connectivityTest;
    if (machine_.isSimulated()) {
        // The operator's part: the simulated plate goes under the bit.
        const auto commands = probe::probeCommands(machine_.settings().probe.plateType);
        if (selected_ < static_cast<int>(commands.size())) {
            machine_.placeSimulatedPlate(static_cast<probe::ProbeType>(probeTypeIndex()), toolDiameter(), corner(),
                                         commands[selected_].axes);
        }
    }
    Q_EMIT changed();
}

void ProbeModel::confirmCircuit() {
    checked_ = true;
    Q_EMIT changed();
}

bool ProbeModel::start() {
    const auto commands = probe::probeCommands(machine_.settings().probe.plateType);
    if (!circuitChecked() || !canClick() || selected_ >= static_cast<int>(commands.size())) {
        return false;
    }
    if (commands[selected_].needsTool && probeTypeIndex() == static_cast<int>(probe::ProbeType::Diameter) &&
        toolDiameter() <= 0) {
        return false;
    }
    std::vector<std::string> code = machine_.probeRoutine(
        commands[selected_].axes, static_cast<probe::ProbeType>(probeTypeIndex()), toolDiameter(), corner());
    return machine_.runProbe(std::move(code));
}

}  // namespace gs::ui
