#include "job_service.hpp"
#include "machine.hpp"
#include "gs/controller/controller.hpp"
#include "gs/job/outline.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace gs::app {

JobService::JobService(Machine& machine, QObject* parent)
    : QObject(parent), machine_(machine) {}

bool JobService::loadFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QFileInfo info(path);
    machine_.loadProgram(info.fileName(), std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                         info.absoluteFilePath());
    AppSettings settings = machine_.settings();
    addRecentFile(settings.recentFiles, {info.fileName().toStdString(), info.absoluteFilePath().toStdString(),
                                         info.size(), QDateTime::currentMSecsSinceEpoch()});
    machine_.setSettings(settings);
    return true;
}

void JobService::forgetRecentFile(const QString& path) {
    AppSettings settings = machine_.settings();
    std::erase_if(settings.recentFiles, [&path](const RecentFile& f) { return f.filePath == path.toStdString(); });
    machine_.setSettings(settings);
}

void JobService::clearRecentFiles() {
    AppSettings settings = machine_.settings();
    settings.recentFiles.clear();
    machine_.setSettings(settings);
}

expr::Value JobService::fileContext() const {
    const Toolpath& toolpath = machine_.toolpath();
    double min[3] = {0, 0, 0};
    double max[3] = {0, 0, 0};
    bool any = false;
    for (const std::vector<float>* segments : {&toolpath.rapids, &toolpath.feeds}) {
        for (std::size_t i = 0; i + 2 < segments->size(); i += 3) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const double v = (*segments)[i + axis];
                min[axis] = any ? std::min(min[axis], v) : v;
                max[axis] = any ? std::max(max[axis], v) : v;
            }
            any = true;
        }
    }
    expr::Value context = expr::Value::object();
    context.set("xmin", expr::Value(min[0]));
    context.set("xmax", expr::Value(max[0]));
    context.set("ymin", expr::Value(min[1]));
    context.set("ymax", expr::Value(max[1]));
    context.set("zmin", expr::Value(min[2]));
    context.set("zmax", expr::Value(max[2]));
    return context;
}

bool JobService::runOutline(QString* error) {
    const auto fail = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    controller::Controller* c = machine_.controller();
    if (!c || !machine_.hasProgram() || machine_.isAnalyzing() || !c->workflow().isIdle() || c->state().status.activeState != "Idle") {
        return fail(tr("Load a file and wait for an idle machine."));
    }
    job::OutlineInput input;
    const AppSettings& s = machine_.settings();
    input.mode = s.outlineMode;
    input.isLaser = s.spindle.laser.onOutline && machine_.laserMode();
    input.outlineSpeed = s.outlineSpeed;
    input.bbox = machine_.analysis().bounds;
    input.content = machine_.programText();

    const Toolpath& toolpath = machine_.toolpath();
    std::vector<std::pair<std::uint32_t, const float*>> segments;
    for (std::size_t i = 0; i < toolpath.rapidLines.size(); ++i) {
        segments.emplace_back(toolpath.rapidLines[i], &toolpath.rapids[i * 6]);
    }
    for (std::size_t i = 0; i < toolpath.feedLines.size(); ++i) {
        segments.emplace_back(toolpath.feedLines[i], &toolpath.feeds[i * 6]);
    }
    std::stable_sort(segments.begin(), segments.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [line, segment] : segments) {
        input.vertices.insert(input.vertices.end(), segment, segment + 6);
    }

    const std::string homing = c->runner().setting("$22");
    const bool homingEnabled = !homing.empty() && homing != "0";
    const double zMpos = std::fabs(c->runner().machinePosition()[2]);
    input.zTravel = homingEnabled ? std::min(zMpos - 1, 5.0) : 5.0;
    const auto program = job::outlineProgram(input);
    if (!program) {
        return fail(tr("The file has no toolpath to outline."));
    }
    c->gcode(*program, fileContext());
    Q_EMIT machine_.successNotice(tr("Running file outline"));
    return true;
}

bool JobService::startFromLine(std::size_t line, double safeHeight) {
    controller::Controller* c = machine_.controller();
    if (!c || !machine_.hasProgram() || machine_.isAnalyzing() || !c->workflow().isIdle()) {
        return false;
    }
    controller::StartOptions options;
    options.lineToStartFrom = line;
    options.zMax = machine_.analysis().bounds.max.z;
    options.safeHeight = safeHeight;
    options.spindleDelay = machine_.settings().preferences.spindleDelay;
    c->start(options);
    return true;
}

}  // namespace gs::app
