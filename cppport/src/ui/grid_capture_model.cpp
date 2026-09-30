#include "grid_capture_model.hpp"

#include "machine.hpp"

#include "gs/controller/controller.hpp"
#include "gs/sim/grbl_simulator.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <QDate>
#include <QFile>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace gs::ui {
namespace {

QString localPath(const QString& file) {
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

}  // namespace

GridCaptureModel::GridCaptureModel(QObject* parent)
    : UiModelBase(parent), capture_([this](std::vector<std::string> lines) { send(std::move(lines)); }) {
    connectMachineSignals(false, true);
    timer_.setSingleShot(true);
    timer_.setInterval(kProbeTimeoutMs);
    connect(&timer_, &QTimer::timeout, this, [this] {
        capture_.fail(tr("Timed out waiting for a probe result").toStdString());
        Q_EMIT changed();
    });
    // [PRB:...] is read from the board's lines rather than from the
    // controller's parameters, as the branch does: GrblRunner only reported
    // a parameter when its value changed, so two points at the same height
    // gave one update and the loop waited forever for the second.
    connect(&machine_, &app::Machine::rawLine, this, [this](const QString& line) {
        if (capture_.onLine(line.toStdString())) {
            if (!capture_.running()) {
                timer_.stop();
            }
            Q_EMIT changed();
        }
    });
    connect(&machine_, &app::Machine::connectionChanged, this, [this] {
        if (capture_.running() && !connected()) {
            timer_.stop();
            capture_.fail(tr("The connection closed").toStdString());
            Q_EMIT changed();
        }
    });
}

QString GridCaptureModel::status() const {
    switch (capture_.status()) {
        case probe::GridCapture::Status::Running: return "running";
        case probe::GridCapture::Status::Done: return "done";
        case probe::GridCapture::Status::Stopped: return "stopped";
        case probe::GridCapture::Status::Failed: return "failed";
        case probe::GridCapture::Status::Idle: break;
    }
    return "idle";
}

QString GridCaptureModel::statusText() const {
    switch (capture_.status()) {
        case probe::GridCapture::Status::Running:
            return tr("Probing point %1 of %2").arg(captured() + 1).arg(total());
        case probe::GridCapture::Status::Done:
            return tr("Captured %1 points").arg(captured());
        case probe::GridCapture::Status::Stopped:
            return tr("Stopped after %1 of %2 points").arg(captured()).arg(total());
        case probe::GridCapture::Status::Failed:
            return capture_.error().empty() ? tr("Capture failed") : QString::fromStdString(capture_.error());
        case probe::GridCapture::Status::Idle: break;
    }
    return {};
}

QString GridCaptureModel::lastPoint() const {
    const auto& points = capture_.points();
    if (points.empty()) {
        return {};
    }
    const probe::CapturedPoint& p = points.back();
    return QString("X%1 Y%2 Z%3").arg(positionText(p.x), positionText(p.y), positionText(p.z));
}

bool GridCaptureModel::simulated() const {
    return machine_.isSimulated();
}

void GridCaptureModel::begin() {
    sim::GrblSimulator* simulator = machine_.simulator();
    if (!simulator || capture_.running()) {
        return;
    }
    // 20 mm tiles, 10 mm below the probe, falling 2 mm per 100 mm in X and
    // rising 1 mm per 100 mm in Y. The tiles' edges sit 5 mm from the
    // default 10 mm grid's points, so no point lands on a step.
    const sim::SimAxes at = simulator->machinePosition();
    std::vector<sim::Solid> tiles;
    for (int i = -5; i < 20; ++i) {
        for (int j = -5; j < 20; ++j) {
            const double x0 = at[0] - 15 + 20.0 * i;
            const double y0 = at[1] - 15 + 20.0 * j;
            const double top = at[2] - 10 - 0.02 * (x0 + 10 - at[0]) + 0.01 * (y0 + 10 - at[1]);
            tiles.push_back({{x0, y0, top - 20}, {x0 + 20, y0 + 20, top}});
        }
    }
    simulator->setProbeSolids(std::move(tiles));
    simulator->setToolRadius(machine_.settings().probe.tipDiameter3D / 2);
}

std::optional<probe::CaptureSetup> GridCaptureModel::setup() const {
    controller::Controller* c = machine_.controller();
    if (!c || !canClick() || capture_.running()) {
        return std::nullopt;
    }
    probe::CaptureSetup s;
    const auto work = machine_.workPositionMm();
    const auto mpos = machine_.machinePositionMm();
    for (std::size_t i = 0; i < 3; ++i) {
        s.work[i] = work[i];
        s.machine[i] = mpos[i];
    }
    s.inches = c->runner().modal().units == "G20";
    s.reportInches = c->runner().setting("$13") == "1";
    s.feedrate = machine_.settings().probe.probeFastFeedrate;
    s.probeDistance = machine_.settings().probe.zProbeDistance;
    return s;
}

bool GridCaptureModel::startGrid(const QString& dx, const QString& nx, const QString& dy, const QString& ny) {
    const std::optional<probe::CaptureSetup> s = setup();
    if (!s) {
        return false;
    }
    // parseInt, clamped to 1-200 (the branch's clampInt); a count that is
    // not a number is 1.
    const auto count = [](const QString& text) {
        const double value = js::parseInt(text.toStdString());
        return std::isnan(value) ? 1 : static_cast<int>(std::clamp(value, 1.0, 200.0));
    };
    const double spacingX = js::stringToNumber(dx.toStdString());
    const double spacingY = js::stringToNumber(dy.toStdString());
    if (!std::isfinite(spacingX) || !std::isfinite(spacingY)) {
        return false;
    }
    const auto mm = [this](double value) { return metric() ? value : units::in2mm(value); };
    if (!capture_.startGrid(*s, {mm(spacingX), count(nx), mm(spacingY), count(ny)})) {
        return false;
    }
    timer_.start();
    Q_EMIT changed();
    return true;
}

void GridCaptureModel::send(std::vector<std::string> lines) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcode(lines);
    }
    // Each point's probe gets its own time.
    if (timer_.isActive()) {
        timer_.start();
    }
}

void GridCaptureModel::stop() {
    capture_.stop();
    Q_EMIT changed();
}

void GridCaptureModel::clear() {
    capture_.clear();
    Q_EMIT changed();
}

QString GridCaptureModel::csv() const {
    return QString::fromStdString(probe::pointsToCsv(capture_.points(), metric()));
}

QVariantMap GridCaptureModel::save(const QString& file) {
    if (capture_.points().empty()) {
        return {{"ok", false}, {"message", tr("No points to save")}};
    }
    QString path = localPath(file);
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) {
        path += ".csv";
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {{"ok", false}, {"message", tr("Cannot write %1: %2").arg(out.fileName(), out.errorString())}};
    }
    const std::string text = probe::pointsToCsv(capture_.points(), metric()) + "\n";
    out.write(text.data(), static_cast<qint64>(text.size()));
    return {{"ok", true}, {"message", tr("Saved %1 points").arg(pointCount())}};
}

QString GridCaptureModel::defaultFileName() const {
    return QString("rectangular-grid-%1.csv").arg(QDate::currentDate().toString(Qt::ISODate));
}

}  // namespace gs::ui
