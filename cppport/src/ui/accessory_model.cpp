#include "accessory_model.hpp"

#include "accessory_wizards.hpp"
#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"
#include "gs/controller/locations.hpp"
#include "gs/toolchange/wizards.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <QFile>

#include <algorithm>
#include <cmath>

namespace gs::ui {
namespace {

const QString kTlsHelp = QStringLiteral("https://resources.sienci.com/view/addons-tls/");
const QString kAutoSpinHelp = QStringLiteral("https://resources.sienci.com/view/as-er-collets/");

QVariantMap image(const QString& resource) {
    return {{"kind", "image"}, {"url", "qrc" + resource}};
}

QVariantMap jogging() {
    return {{"kind", "jogging"}};
}

QVariantMap helpLink(const QString& url) {
    return {{"kind", "link"}, {"title", QObject::tr("Need help?")}, {"text", QObject::tr("Follow along in our")},
            {"url", url}};
}

QVariantMap side(const QString& kind, const QString& title = {}) {
    return {{"kind", kind}, {"title", title}};
}

QVariantMap step(const QString& id, const QString& title, const QVariantList& side = {}) {
    return {{"id", id}, {"title", title}, {"side", side}};
}

QVariantMap completion(const QString& done, const QStringList& next, const QString& warning = {}) {
    return {{"done", done}, {"next", next}, {"warning", warning}};
}

struct VacuumTableSize {
    QString value;
    QString label;
    QString program;
    QString name;
};

// VACUUM_TABLE_SIZES / MOUNTING_GCODE_BY_SIZE
const std::vector<VacuumTableSize>& vacuumTableSizeList() {
    static const std::vector<VacuumTableSize> sizes{
        {"4x8", QObject::tr("4' x 8' Vacuum Table"), ":/accessories/4x8HoleMounts.gcode",
         "gSender_Vacuum_Table_Mounting_4x8"},
    };
    return sizes;
}

QVariantMap settingRow(const QString& label, const std::string& value, bool ok, const QString& verdict) {
    return {{"label", label}, {"value", QString::fromStdString(value.empty() ? "-" : value)}, {"ok", ok},
            {"verdict", verdict}};
}

}  // namespace

AccessoryModel::AccessoryModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    for (auto signal : {&app::Machine::connectionChanged, &app::Machine::settingsChanged, &app::Machine::stateChanged,
                        &app::Machine::appSettingsChanged}) {
        connect(&machine_, signal, this, &AccessoryModel::changed);
    }
}

// ---- the board ---------------------------------------------------------------------------------

long long AccessoryModel::firmwareBuild() const {
    controller::Controller* c = machine_.controller();
    return c ? c->runner().settings().semver : -1;
}

std::string AccessoryModel::boardId() const {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return {};
    }
    const auto& info = c->runner().settings().info;
    const auto board = info.find("BOARD");
    return board == info.end() ? std::string() : board->second.text;
}

std::string AccessoryModel::setting(const char* key) const {
    controller::Controller* c = machine_.controller();
    return c ? c->runner().setting(key) : std::string();
}

void AccessoryModel::send(const std::vector<std::string>& code) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcode(code);
    }
}

bool AccessoryModel::atciFirmware() const {
    return firmwareBuild() >= app::kAtciSupportedVersion;
}

bool AccessoryModel::connected() const {
    return machine_.isConnected();
}

QString AccessoryModel::units() const {
    return machine_.settings().metric ? QStringLiteral("mm") : QStringLiteral("in");
}

bool AccessoryModel::probeActive() const {
    controller::Controller* c = machine_.controller();
    return c && c->state().status.probeActive;
}

QVariantList AccessoryModel::machinePosition() const {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return {};
    }
    const auto& mpos = c->runner().machinePosition();
    return {mpos[0], mpos[1], mpos[2]};
}

// ---- the wizards -------------------------------------------------------------------------------

QVariantList AccessoryModel::wizards() const {
    // useAllWizards(), in its order - less the ATC's.
    const QVariantMap commands = side("commands", tr("Commands to be sent"));
    QVariantMap spindleCommands = commands;
    spindleCommands["source"] = "spindle";

    const QVariantMap spindle{
        {"id", "sienci-spindle"},
        {"title", tr("Sienci Spindle")},
        {"image", "qrc:/accessories/spindle_image.png"},
        {"helpUrl", ""},
        {"subWizards",
         QVariantList{QVariantMap{
             {"id", "spindle-config"},
             {"title", tr("Sienci Spindle Config")},
             {"description", tr("Configure your Sienci Spindle for first time use")},
             {"estimatedTime", tr("5 - 30 minutes")},
             {"configVersion", "1.0"},
             {"steps", QVariantList{step("spindle-config", tr("Spindle Config"), {spindleCommands}),
                                    step("modbus-config", tr("Modbus Configuration"))}},
             {"completion",
              completion(tr("Your spindle has been successfully configured and is ready to use."),
                         {tr("Restart your controller using the power switch"),
                          tr("Ensure the VFD is turned on before restarting the controller"),
                          tr("Reconnect in gSender and verify your spindle is working as expected.")})},
         }}},
    };
    QStringList tlsNext;
    if (!atciFirmware()) {
        tlsNext << tr("Restart your controller using the power switch (power cycle)")
                << tr("Reconnect in gSender to finish setting up your Tool Length Sensor.");
    }
    const QVariantMap tls{
        {"id", "sienci-tls"},
        {"title", tr("Sienci TLS")},
        {"image", "qrc:/accessories/TLS_Step_01.png"},
        {"helpUrl", kTlsHelp},
        {"subWizards",
         QVariantList{QVariantMap{
             {"id", "tls-setup"},
             {"title", tr("TLS Setup Wizard")},
             {"description", tr("Configure your Tool Length Sensor and tool change behaviour")},
             {"estimatedTime", tr("5 - 15 minutes")},
             {"configVersion", ""},
             {"steps",
              QVariantList{
                  step("options", tr("Tool Change Options"), {image(":/accessories/TLS_Step_01.png"), helpLink(kTlsHelp)}),
                  step("tls-location", tr("Set TLS Location"),
                       {image(":/accessories/TLS_Step_02.png"), jogging(), helpLink(kTlsHelp)}),
                  step("manual-position", tr("Set Tool Change Location"),
                       {image(":/accessories/TLS_Step_03_Pin.png"), jogging(), helpLink(kTlsHelp)}),
                  step("continuity-check", tr("Verify TLS Continuity"),
                       {side("tlsSettings"), side("tlsInput"), helpLink(kTlsHelp)}),
              }},
             {"completion",
              completion(tr("Your Tool Length Sensor and tool change behaviour have been configured."), tlsNext,
                         tr("If you change your spindle or router's physical position (e.g. reinstalling it or a "
                            "mounting bracket), you may need to update these settings in <b>Config</b> or run this "
                            "installation wizard again."))},
         }}},
    };

    QVariantMap autoSpinCommands = commands;
    autoSpinCommands["source"] = "autospin";
    const QVariantMap autoSpin{
        {"id", "autospin"},
        {"title", tr("AutoSpin")},
        {"image", "qrc:/accessories/AutoSpin_landing.png"},
        {"helpUrl", kAutoSpinHelp},
        {"subWizards",
         QVariantList{QVariantMap{
             {"id", "autospin-config"},
             {"title", tr("AutoSpin Setup")},
             {"description", tr("Configure your AutoSpin for first time use")},
             {"estimatedTime", tr("5 - 15 minutes")},
             {"configVersion", "1.0"},
             {"steps", QVariantList{step("eeprom-config", tr("AutoSpin EEPROM Configuration"),
                                         {autoSpinCommands, helpLink(kAutoSpinHelp)}),
                                    step("test", tr("Test AutoSpin"), {helpLink(kAutoSpinHelp)})}},
             {"completion",
              completion(tr("Your AutoSpin has been successfully configured and is ready to use."),
                         {tr("Restart your controller using the power switch"),
                          tr("Turn the AutoSpin dial to \"S\" and turn on the power toggle"),
                          tr("Reconnect in gSender and verify your spindle starts and stops from the Carve page.")})},
         }}},
    };

    const QVariantMap vacuum{
        {"id", "vacuum-table"},
        {"title", tr("Vacuum Table")},
        {"image", ""},
        {"helpUrl", ""},
        {"subWizards",
         QVariantList{
             QVariantMap{
                 {"id", "mounting-setup"},
                 {"title", tr("Mounting Setup")},
                 {"description", tr("Zero your table and carve the mounting holes for your vacuum table.")},
                 {"estimatedTime", tr("10 - 20 minutes")},
                 {"configVersion", ""},
                 {"steps", QVariantList{step("zero-position", tr("Zero Position"), {jogging()}),
                                        step("select-size", tr("Table Size")),
                                        step("load-mounting-gcode", tr("Load to Carve"))}},
                 {"completion", QVariant()},
             },
             QVariantMap{
                 {"id", "grid-setup"},
                 {"title", tr("Grid Setup")},
                 {"description", tr("Carve an optional alignment grid onto your vacuum table.")},
                 {"estimatedTime", tr("5 minutes")},
                 {"configVersion", ""},
                 {"steps", QVariantList{step("load-grid-gcode", tr("Load Grid to Carve"))}},
                 {"completion", QVariant()},
             },
         }},
    };
    return {spindle, tls, autoSpin, vacuum};
}

QStringList AccessoryModel::failedChecks(const QString& wizard) const {
    controller::Controller* c = machine_.controller();
    const bool connected = machine_.isConnected();
    const bool homed = c && c->hasHomed();
    const bool grblHal = c && c->isGrblHal();
    const QString notConnected =
        tr("Your controller is not connected.  Connect to your CNC to configure this accessory.");
    const QString notHomed =
        tr("Machine not homed. Please home your machine before proceeding with accessory configuration.");
    const QString notHal = tr("You must be connected to a grblHAL device to use this wizard.");
    QStringList reasons;
    const auto check = [&](bool ok, const QString& reason) {
        if (!ok) {
            reasons << reason;
        }
    };
    if (wizard == "sienci-spindle") {
        check(connected, notConnected);
        check(grblHal, notHal);
    } else if (wizard == "sienci-tls") {
        check(connected, notConnected);
        check(grblHal, notHal);
        check(homed, notHomed);
    } else if (wizard == "autospin") {
        check(connected, notConnected);
    } else if (wizard == "vacuum-table") {
        check(connected, notConnected);
        check(homed, notHomed);
    }
    return reasons;
}

bool AccessoryModel::skipStep(const QString& step) const {
    return step == "manual-position" && !machine_.settings().moveToManualPosition;
}

// ---- Vacuum Table ------------------------------------------------------------------------------

QVariantList AccessoryModel::vacuumSizes() const {
    QVariantList list;
    for (const VacuumTableSize& size : vacuumTableSizeList()) {
        list.append(QVariantMap{{"value", size.value}, {"label", size.label}});
    }
    return list;
}

void AccessoryModel::zeroXY() {
    send({"G10 L20 P0 X0 Y0"});
}

bool AccessoryModel::load(const QString& resource, const QString& name) {
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    machine_.loadProgram(name, file.readAll().toStdString());
    return true;
}

bool AccessoryModel::loadVacuumMounting(const QString& size) {
    const std::vector<VacuumTableSize>& sizes = vacuumTableSizeList();
    auto chosen = std::find_if(sizes.begin(), sizes.end(), [&](const VacuumTableSize& s) { return s.value == size; });
    const VacuumTableSize& table = chosen != sizes.end() ? *chosen : sizes.front();
    return load(table.program, table.name);
}

bool AccessoryModel::loadVacuumGrid() {
    return load(":/accessories/Grids.gcode", "gSender_Vacuum_Table_Grid");
}

// ---- Sienci TLS --------------------------------------------------------------------------------

QStringList AccessoryModel::firstToolBehaviours() const {
    QStringList list;
    for (const char* option : toolchange::kFirstToolBehaviours) {
        list << QString::fromLatin1(option);
    }
    return list;
}

void AccessoryModel::applyTlsOptions(const QString& firstTool, bool manualLocation) {
    std::vector<std::string> code{"$6=1"};
    if (!atciFirmware()) {
        code.emplace_back("$668=0");
    }
    if (boardId() == "SLB Lite") {
        const double value = js::stringToNumber(setting("$65"));
        const long long current = std::isfinite(value) ? static_cast<long long>(value) : 0;
        const long long updated = current | 8;
        if (updated != current) {
            code.push_back("$65=" + std::to_string(updated));
        }
        code.emplace_back("G65 P5 Q1");
    }
    code.emplace_back("$$");
    send(code);
    app::AppSettings settings = machine_.settings();
    settings.toolChange.option = "Fixed Tool Sensor";
    settings.moveToManualPosition = manualLocation;
    settings.firstToolBehaviour = firstTool.toStdString();
    settings.toolChange.passthrough = false;
    settings.probe.probeFastFeedrate = 1000;
    machine_.setSettings(settings);  // updateToolchangeContext()
}

QString AccessoryModel::positionText(double mm) const {
    const app::AppSettings& settings = machine_.settings();
    return QString::fromStdString(units::positionText(mm, settings.metric, settings.customDecimalPlaces));
}

double AccessoryModel::positionMm(const QString& text) const {
    const double value = js::stringToNumber(text.toStdString());
    return machine_.settings().metric ? value : units::in2mm(value);
}

void AccessoryModel::setTlsLocation(double x, double y, double z) {
    app::AppSettings settings = machine_.settings();
    settings.toolChangePosition = {x, y, z};
    machine_.setSettings(settings);
    // G10 L2 P9: the sensor's X/Y as the ninth work offset; $# shows it.
    send({"G21 G10 L2 P9 X" + js::numberToString(x) + " Y" + js::numberToString(y), "$#"});
}

void AccessoryModel::setManualPosition(double x, double y, double z) {
    app::AppSettings settings = machine_.settings();
    settings.manualPosition = {x, y, z};
    machine_.setSettings(settings);
}

controller::LocationSettings AccessoryModel::locationSettings() const {
    controller::LocationSettings s;
    s.homing = setting("$22");
    s.homingDirMask = setting("$23");
    s.pullOff = setting("$27");
    s.xMaxTravel = setting("$130");
    s.yMaxTravel = setting("$131");
    return s;
}

QVariantList AccessoryModel::recommendedManualPosition() const {
    controller::Controller* c = machine_.controller();
    const auto at = controller::defaultToolChangePosition(locationSettings(), c && c->homingFlag());
    if (!at) {
        return {0.0, 0.0, 0.0};
    }
    return {at->x, at->y, at->z};
}

void AccessoryModel::goToPosition(double x, double y, double z) {
    send(controller::parkCommands({x, y, z}, locationSettings()));
}

namespace {
QString okText(bool ok, const QString& expected) {
    return ok ? QObject::tr("OK") : expected;
}
}  // namespace

QVariantList AccessoryModel::tlsSettings() const {
    const std::string invert = setting("$6");
    const bool invertOk = js::stringToNumber(invert) == 1;
    QVariantList rows{settingRow(tr("$6 - Invert Probe Pin"), invert, invertOk, okText(invertOk, tr("Expected 1")))};
    if (!atciFirmware()) {
        const std::string legacy = setting("$668");
        const bool legacyOk = js::stringToNumber(legacy) == 0;
        rows.append(settingRow(tr("$668 - Legacy Tool Sensor"), legacy, legacyOk, okText(legacyOk, tr("Expected 0"))));
    }
    return rows;
}

bool AccessoryModel::tlsInputShown() const {
    const double inputs = js::stringToNumber(setting("$65"));
    const bool enabled = std::isfinite(inputs) && (static_cast<long long>(inputs) & 8) != 0;
    return boardId() == "SLB Lite" && enabled;
}

bool AccessoryModel::tlsInputReady() const {
    controller::Controller* c = machine_.controller();
    return c && c->state().status.probe && c->state().status.probe->type == 1;
}

QVariantList AccessoryModel::tlsInput() const {
    const std::string invert = setting("$6");
    const bool invertOk = js::stringToNumber(invert) == 1;
    controller::Controller* c = machine_.controller();
    const int probeType = c && c->state().status.probe ? c->state().status.probe->type : 0;
    return {settingRow(tr("$6 - Invert Probe Pin"), invert, invertOk, okText(invertOk, tr("Expected 1"))),
            settingRow(tr("Probe Type"), std::to_string(probeType), probeType == 1,
                       probeType == 1 ? tr("OK") : tr("Not Ready"))};
}

void AccessoryModel::enableTlsInput() {
    send({"G65 P5 Q1"});
}

// ---- AutoSpin ----------------------------------------------------------------------------------

namespace {
QVariantMap preview(const QString& label, const std::vector<std::string>& lines) {
    QStringList list;
    for (const std::string& line : lines) {
        list << QString::fromStdString(line);
    }
    return {{"label", label}, {"lines", list}};
}
}  // namespace

QVariantMap AccessoryModel::autoSpinPreview() const {
    controller::Controller* c = machine_.controller();
    const bool grblHal = c && c->isGrblHal();
    const bool slbLite = boardId() == "SLB Lite";
    return preview(grblHal ? (slbLite ? tr("grblHAL (slb-lite)") : tr("grblHAL")) : tr("Other Firmware"),
                   app::autoSpinCommands(grblHal, slbLite));
}

void AccessoryModel::applyAutoSpin() {
    controller::Controller* c = machine_.controller();
    send(app::autoSpinCommands(c && c->isGrblHal(), boardId() == "SLB Lite"));
}

namespace {
int readSetting(const std::string& text, double fallback) {
    const double value = text.empty() ? fallback : js::stringToNumber(text);
    return std::isfinite(value) ? static_cast<int>(value) : static_cast<int>(fallback);
}
}  // namespace

int AccessoryModel::spindleMin() const {
    return readSetting(setting("$31"), 1000);
}

int AccessoryModel::spindleMax() const {
    return readSetting(setting("$30"), 30000);
}

bool AccessoryModel::spindleRunning() const {
    controller::Controller* c = machine_.controller();
    return c && c->runner().modal().spindle != "M5";
}

double AccessoryModel::spindleReported() const {
    controller::Controller* c = machine_.controller();
    return c ? c->state().status.spindle : 0;
}

void AccessoryModel::startSpindle(int rpm) {
    send({"M3 S" + std::to_string(rpm)});
}

void AccessoryModel::changeSpindleSpeed(int rpm) {
    if (controller::Controller* c = machine_.controller()) {
        c->spindleSpeedChange(rpm);
    }
}

void AccessoryModel::stopSpindle() {
    send({"M5"});
}

// ---- Sienci Spindle ----------------------------------------------------------------------------

QVariantMap AccessoryModel::spindlePreview() const {
    const long long build = firmwareBuild();
    return preview(build >= app::kAtciSupportedVersion ? tr("grblHAL (>%1)").arg(app::kAtciSupportedVersion)
                                                       : tr("sienciHAL (< %1)").arg(app::kAtciSupportedVersion),
                   app::sienciSpindleCommands(build));
}

void AccessoryModel::applySpindle() {
    send(app::sienciSpindleCommands(firmwareBuild()));
    app::AppSettings settings = machine_.settings();
    settings.spindleFunctions = true;  // the Spindle/Laser tab
    machine_.setSettings(settings);
}

void AccessoryModel::applyModbus() {
    send(app::modbusCommands(firmwareBuild()));
}

}  // namespace gs::ui
