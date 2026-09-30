#include "gs/probe/grid_capture.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <boost/regex.hpp>

#include <algorithm>
#include <cmath>

namespace gs::probe {

std::optional<ProbeReport> parseProbeReport(std::string_view line) {
    // [PRB:0.000,0.000,-10.000:1] - the branch's PRB_RE, with the space its
    // trim() took, and further axes allowed (grblHAL reports A as a fourth).
    static const boost::regex re(R"(^\s*\[PRB:(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)(?:,-?[\d.]+)*:([01])\]\s*$)");
    boost::match_results<std::string_view::const_iterator> m;
    if (!boost::regex_match(line.begin(), line.end(), m, re)) {
        return std::nullopt;
    }
    ProbeReport report;
    for (int i = 0; i < 3; ++i) {
        report.position[static_cast<std::size_t>(i)] = js::stringToNumber(m[i + 1].str());
    }
    report.contact = m[4].str() == "1";
    return report;
}

std::vector<std::pair<int, int>> serpentine(int nx, int ny) {
    std::vector<std::pair<int, int>> order;
    if (nx <= 0 || ny <= 0) {
        return order;
    }
    order.reserve(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny));
    for (int iy = 0; iy < ny; ++iy) {
        const bool forward = iy % 2 == 0;
        for (int step = 0; step < nx; ++step) {
            order.emplace_back(forward ? step : nx - 1 - step, iy);
        }
    }
    return order;
}

std::string pointsToCsv(const std::vector<CapturedPoint>& points, bool metric) {
    std::vector<CapturedPoint> rows = points;
    std::stable_sort(rows.begin(), rows.end(), [](const CapturedPoint& a, const CapturedPoint& b) {
        return a.iy != b.iy ? a.iy < b.iy : a.ix < b.ix;
    });
    const int digits = metric ? 3 : 4;
    const auto value = [&](double mm) { return js::toFixed(metric ? mm : units::mm2in(mm), digits); };
    std::string csv = "X,Y,Z";
    for (const CapturedPoint& p : rows) {
        csv += '\n';
        csv += value(p.x) + ',' + value(p.y) + ',' + value(p.z);
    }
    return csv;
}

GridCapture::GridCapture(Send send) : send_(std::move(send)) {}

std::string GridCapture::num(double mm) const {
    return setup_.inches ? js::toFixed(units::mm2in(mm), 4) : js::toFixed(mm, 3);
}

bool GridCapture::startGrid(const CaptureSetup& setup, const GridSpec& grid) {
    if (running() || !std::isfinite(grid.dx) || !std::isfinite(grid.dy)) {
        return false;
    }
    std::vector<std::pair<int, int>> order = serpentine(grid.nx, grid.ny);
    if (order.empty()) {
        return false;
    }
    points_.clear();
    setup_ = setup;
    grid_ = grid;
    order_ = std::move(order);
    // [PRB:] is in machine coordinates. Offsets do not move during a run,
    // so one conversion taken up front holds for every point.
    for (std::size_t i = 0; i < 3; ++i) {
        offset_[i] = setup.machine[i] - setup.work[i];
    }
    next_ = 0;
    stopRequested_ = false;
    error_.clear();
    status_ = Status::Running;
    sendNext();
    return true;
}

std::array<double, 2> GridCapture::target() const {
    if (order_.empty()) {
        return {setup_.work[0], setup_.work[1]};
    }
    const auto& [ix, iy] = order_[static_cast<std::size_t>(std::min<int>(next_, total() - 1))];
    return {setup_.work[0] + ix * grid_.dx, setup_.work[1] + iy * grid_.dy};
}

void GridCapture::sendNext() {
    const std::string units = setup_.inches ? "G20" : "G21";
    const double floor = setup_.work[2] - std::abs(setup_.probeDistance);
    // The probe is the last line queued: anything behind it would reach an
    // alarm-locked machine if the probe missed, and come back as error:9.
    // The retract is the first line of the next point instead, and the last
    // point's is sent as the run ends.
    const auto [x, y] = target();
    send_({units + " G90 G0 Z" + num(setup_.work[2]), units + " G90 G0 X" + num(x) + " Y" + num(y),
           units + " G90 G38.2 Z" + num(floor) + " F" + num(setup_.feedrate)});
}

void GridCapture::stop() {
    if (running()) {
        stopRequested_ = true;
    }
}

bool GridCapture::onLine(std::string_view line) {
    if (!running()) {
        return false;
    }
    if (const std::optional<ProbeReport> report = parseProbeReport(line)) {
        const auto [x, y] = target();
        if (!report->contact) {
            fail("No contact at X" + num(x) + " Y" + num(y) + " - nothing within " +
                 num(std::abs(setup_.probeDistance)) + " below the start height");
            return true;
        }
        CapturedPoint point;
        std::tie(point.ix, point.iy) = order_[static_cast<std::size_t>(next_)];
        const double scale = setup_.reportInches ? 25.4 : 1;
        point.x = report->position[0] * scale - offset_[0];
        point.y = report->position[1] * scale - offset_[1];
        point.z = report->position[2] * scale - offset_[2];
        points_.push_back(point);
        ++next_;
        if (next_ >= total()) {
            finish(Status::Done);
        } else if (stopRequested_) {
            finish(Status::Stopped);
        } else {
            sendNext();
        }
        return true;
    }
    const std::string_view trimmed = line.substr(0, line.find_last_not_of(" \r\n") + 1);
    if (trimmed.starts_with("ALARM:") || trimmed.starts_with("error:")) {
        fail(std::string(trimmed));
        return true;
    }
    return false;
}

void GridCapture::finish(Status status) {
    status_ = status;
    // Clear of the work on the way out.
    send_({(setup_.inches ? "G20" : "G21") + std::string(" G90 G0 Z") + num(setup_.work[2])});
}

void GridCapture::fail(std::string message) {
    if (!running()) {
        return;
    }
    // No retract: a failure usually leaves the machine alarm-locked, where a
    // move only earns "error:9".
    status_ = Status::Failed;
    error_ = std::move(message);
}

void GridCapture::clear() {
    if (!running()) {
        points_.clear();
        order_.clear();
        next_ = 0;
        error_.clear();
        status_ = Status::Idle;
    }
}

}  // namespace gs::probe
