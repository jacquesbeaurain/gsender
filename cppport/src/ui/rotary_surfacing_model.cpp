#include "rotary_surfacing_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"

#include <algorithm>

namespace gs::ui {
namespace {

QVariantMap toMap(const rotary::StockTurningOptions& o) {
    return {{"stockLength", o.stockLength}, {"startHeight", o.startHeight}, {"finalHeight", o.finalHeight},
            {"stepdown", o.stepdown},       {"bitDiameter", o.bitDiameter}, {"toolNumber", o.toolNumber},
            {"stepover", o.stepover},       {"feedrate", o.feedrate},       {"spindleRPM", o.spindleRPM},
            {"shouldDwell", o.shouldDwell}, {"enableRehoming", o.enableRehoming}};
}

}  // namespace

RotarySurfacingModel::RotarySurfacingModel(QObject* parent)
    : QObject(parent), machine_(UiBackend::instance()->machine()) {
    const rotary::StockTurningOptions& stored = machine_.settings().rotary.stockTurning;
    options_ = machine_.settings().metric ? stored : rotary::toImperial(stored);
    connect(&machine_, &app::Machine::stateChanged, this, &RotarySurfacingModel::stateChanged);
    connect(&machine_, &app::Machine::connectionChanged, this, &RotarySurfacingModel::stateChanged);
}

QVariantMap RotarySurfacingModel::options() const {
    return toMap(options_);
}

QVariantMap RotarySurfacingModel::defaults() const {
    return toMap(machine_.settings().metric ? rotary::StockTurningOptions{}
                                            : rotary::toImperial(rotary::StockTurningOptions{}));
}

QString RotarySurfacingModel::units() const {
    return machine_.settings().metric ? QStringLiteral("mm") : QStringLiteral("in");
}

bool RotarySurfacingModel::free() const {
    controller::Controller* c = machine_.controller();
    if (!c || c->state().status.activeState.empty()) {
        return true;
    }
    const std::string& state = c->state().status.activeState;
    return state == "Idle" || state == "Jog";
}

void RotarySurfacingModel::setOption(const QString& key, const QVariant& value) {
    rotary::StockTurningOptions& o = options_;
    const double number = value.toDouble();
    if (key == "stockLength") o.stockLength = number;
    else if (key == "startHeight") o.startHeight = number;
    else if (key == "finalHeight") o.finalHeight = number;
    else if (key == "stepdown") o.stepdown = number;
    else if (key == "bitDiameter") o.bitDiameter = number;
    else if (key == "toolNumber") o.toolNumber = std::max(0, value.toInt());
    else if (key == "stepover") o.stepover = number;
    else if (key == "feedrate") o.feedrate = number;
    else if (key == "spindleRPM") o.spindleRPM = number;
    else if (key == "shouldDwell") o.shouldDwell = value.toBool();
    else if (key == "enableRehoming") o.enableRehoming = value.toBool();
    else return;
    Q_EMIT optionsChanged();
}

void RotarySurfacingModel::save() {
    app::AppSettings settings = machine_.settings();
    settings.rotary.stockTurning = settings.metric ? options_ : rotary::toMetric(options_);
    machine_.setSettings(settings);
}

void RotarySurfacingModel::generate() {
    if (!free()) {
        return;
    }
    save();
    program_ = QString::fromStdString(
        rotary::stockTurningProgram(options_, machine_.settings().metric, machine_.rotaryMode()));
    Q_EMIT programChanged();
}

bool RotarySurfacingModel::load() {
    if (program_.isEmpty() || !free()) {
        return false;
    }
    machine_.loadProgram("gSender_Rotary_Surfacing", program_.toStdString());
    return true;
}

}  // namespace gs::ui
