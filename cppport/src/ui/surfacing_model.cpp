#include "surfacing_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"
#include "gs/util/jsnumber.hpp"

#include <algorithm>

namespace gs::ui {
namespace {

constexpr std::pair<surfacing::StartPosition, const char*> kStarts[] = {
    {surfacing::StartPosition::BackLeft, "backLeft"},   {surfacing::StartPosition::BackRight, "backRight"},
    {surfacing::StartPosition::FrontLeft, "frontLeft"}, {surfacing::StartPosition::FrontRight, "frontRight"},
    {surfacing::StartPosition::Center, "center"},
};

QVariantMap toMap(const surfacing::Options& o) {
    QString start = "backLeft";
    for (const auto& [value, name] : kStarts) {
        if (value == o.startPosition) {
            start = name;
        }
    }
    return {
        {"width", o.width},
        {"length", o.length},
        {"skimDepth", o.skimDepth},
        {"maxDepth", o.maxDepth},
        {"bitDiameter", o.bitDiameter},
        {"toolNumber", o.toolNumber},
        {"stepover", o.stepover},
        {"feedrate", o.feedrate},
        {"spindleRPM", o.spindleRPM},
        {"spindle", QString::fromStdString(o.spindle)},
        {"shouldDwell", o.shouldDwell},
        {"mist", o.mist},
        {"flood", o.flood},
        {"startPosition", start},
        {"pattern", o.type == surfacing::Pattern::ZigZag ? "zigzag" : "spiral"},
        {"cutDirectionFlipped", o.cutDirectionFlipped},
    };
}

}  // namespace

SurfacingModel::SurfacingModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    const surfacing::Options& stored = machine_.settings().surfacing;
    options_ = machine_.settings().metric ? stored : surfacing::toImperial(stored);
    connect(&machine_, &app::Machine::stateChanged, this, &SurfacingModel::stateChanged);
    connect(&machine_, &app::Machine::connectionChanged, this, &SurfacingModel::stateChanged);
}

QVariantMap SurfacingModel::options() const {
    return toMap(options_);
}

QVariantMap SurfacingModel::defaults() const {
    return toMap(machine_.settings().metric ? surfacing::Options{} : surfacing::toImperial(surfacing::Options{}));
}

QString SurfacingModel::units() const {
    return machine_.settings().metric ? QStringLiteral("mm") : QStringLiteral("in");
}

QString SurfacingModel::depthWarning() const {
    if (options_.skimDepth <= options_.maxDepth) {
        return {};
    }
    return tr("Cut depth (%1 %3) exceeds max depth (%2 %3)")
        .arg(QString::fromStdString(js::numberToString(options_.skimDepth)),
             QString::fromStdString(js::numberToString(options_.maxDepth)), units());
}

bool SurfacingModel::free() const {
    // Upstream disables the tool unless the machine is idle or jogging (or
    // not reporting at all).
    controller::Controller* c = machine_.controller();
    if (!c || c->state().status.activeState.empty()) {
        return true;
    }
    const std::string& state = c->state().status.activeState;
    return state == "Idle" || state == "Jog";
}

void SurfacingModel::setOption(const QString& key, const QVariant& value) {
    surfacing::Options& o = options_;
    const double number = value.toDouble();
    if (key == "width") o.width = number;
    else if (key == "length") o.length = number;
    else if (key == "skimDepth") o.skimDepth = number;
    else if (key == "maxDepth") o.maxDepth = number;
    else if (key == "bitDiameter") o.bitDiameter = number;
    else if (key == "toolNumber") o.toolNumber = std::max(0, value.toInt());
    else if (key == "stepover") o.stepover = number;
    else if (key == "feedrate") o.feedrate = number;
    else if (key == "spindleRPM") o.spindleRPM = number;
    else if (key == "spindle") o.spindle = value.toString() == "M4" ? "M4" : "M3";
    else if (key == "shouldDwell") o.shouldDwell = value.toBool();
    else if (key == "mist") o.mist = value.toBool();
    else if (key == "flood") o.flood = value.toBool();
    else if (key == "pattern") o.type = value.toString() == "zigzag" ? surfacing::Pattern::ZigZag : surfacing::Pattern::Spiral;
    else if (key == "cutDirectionFlipped") o.cutDirectionFlipped = value.toBool();
    else if (key == "startPosition") {
        for (const auto& [start, name] : kStarts) {
            if (value.toString() == QLatin1String(name)) {
                o.startPosition = start;
            }
        }
    } else {
        return;
    }
    Q_EMIT optionsChanged();
}

void SurfacingModel::save() {
    app::AppSettings settings = machine_.settings();
    settings.surfacing = settings.metric ? options_ : surfacing::toMetric(options_);
    machine_.setSettings(settings);
}

void SurfacingModel::generate() {
    if (!free()) {
        return;
    }
    save();
    program_ = QString::fromStdString(surfacing::generate(options_, machine_.settings().metric));
    Q_EMIT programChanged();
}

bool SurfacingModel::load() {
    if (program_.isEmpty() || !free()) {
        return false;
    }
    machine_.loadProgram("gSender_Surfacing.gcode", program_.toStdString());
    return true;
}

}  // namespace gs::ui
