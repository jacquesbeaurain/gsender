#pragma once

// Rectangular grid capture with the 3D probe (the feature/probe-mesh-capture
// branch's features/Probe/useMeshCapture.ts, called a "mesh" there): walks a
// grid of points, probing down at each one and collecting where the probe
// touched, for a CSV of X,Y,Z rows. The port adds single points captured
// where the operator has jogged the probe to, into the same list.
//
// The capture is a state machine over the board's lines: start a run, send
// what it asks for, feed it every line the board says; it is Qt-free and
// knows nothing of time (the caller fails a run that goes quiet).

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::probe {

// A "[PRB:x,y,z:flag]" report: machine coordinates in the board's reporting
// units ($13), and whether the probe touched (the flag is 0 on a miss).
struct ProbeReport {
    std::array<double, 3> position{};
    bool contact = false;
};
std::optional<ProbeReport> parseProbeReport(std::string_view line);

// The order a grid's points are visited in: a serpentine, every other row
// right to left, so the tool is not dragged back across the work at the end
// of each row. Pairs of (ix, iy).
std::vector<std::pair<int, int>> serpentine(int nx, int ny);

// A captured point, in work coordinates (mm). Grid points carry their grid
// indices; a point captured by hand has none (-1).
struct CapturedPoint {
    int ix = -1;
    int iy = -1;
    double x = 0;
    double y = 0;
    double z = 0;
    bool manual() const noexcept { return ix < 0; }
};

// "X,Y,Z" and a row per point in the workspace units (3 decimals in mm, 4
// in inches): the grid in grid order (rows along X, then up Y) whatever the
// order it was probed in, then the manual points in the order captured.
std::string pointsToCsv(const std::vector<CapturedPoint>& points, bool metric);

struct GridSpec {
    double dx = 10;  // mm between columns; negative runs towards -X
    int nx = 4;
    double dy = 10;  // mm between rows
    int ny = 4;
};

// Where a run starts and how it probes.
struct CaptureSetup {
    std::array<double, 3> work{};     // work position (mm): the first point and the safe height
    std::array<double, 3> machine{};  // machine position (mm) at the same moment
    bool inches = false;              // the workspace's G20: the moves are written in inches
    bool reportInches = false;        // $13=1: [PRB:] comes in inches
    double feedrate = 150;            // mm/min for the probing move
    double probeDistance = 30;        // mm the probe may travel down before it gives up
};

class GridCapture {
public:
    enum class Status { Idle, Running, Done, Stopped, Failed };
    enum class Kind { Grid, Point };
    using Send = std::function<void(std::vector<std::string>)>;

    explicit GridCapture(Send send);

    // Probes a grid from the setup's position in +X/+Y (the spacing's
    // signs), replacing the grid points from before; manual points stay.
    // False while a run is on, or for an empty grid.
    bool startGrid(const CaptureSetup& setup, const GridSpec& grid);
    // Probes once, straight down from where the probe is, and adds the point.
    bool capturePoint(const CaptureSetup& setup);
    // Graceful: the probe in flight finishes and is kept, then the tool
    // retracts and the run ends.
    void stop();
    // A line from the board. True when the run moved on.
    bool onLine(std::string_view line);
    // Ends a running run as failed (a timeout, a lost connection): nothing
    // more is sent.
    void fail(std::string message);
    // Forgets every point (not while running).
    void clear();

    Status status() const noexcept { return status_; }
    Kind kind() const noexcept { return kind_; }
    bool running() const noexcept { return status_ == Status::Running; }
    const std::vector<CapturedPoint>& points() const noexcept { return points_; }
    int total() const noexcept { return static_cast<int>(order_.size()); }  // the run's points
    int captured() const noexcept { return next_; }                         // of them, done
    const std::string& error() const noexcept { return error_; }
    // The point being probed, work mm (meaningful while running).
    std::array<double, 2> target() const;

private:
    bool begin(const CaptureSetup& setup, Kind kind, std::vector<std::pair<int, int>> order);
    void sendNext();
    void finish(Status status);
    std::string num(double mm) const;

    Send send_;
    Status status_ = Status::Idle;
    Kind kind_ = Kind::Grid;
    CaptureSetup setup_;
    GridSpec grid_;
    std::array<double, 3> offset_{};  // machine - work (mm)
    std::vector<std::pair<int, int>> order_;
    int next_ = 0;
    bool stopRequested_ = false;
    std::vector<CapturedPoint> points_;
    std::string error_;
};

}  // namespace gs::probe
