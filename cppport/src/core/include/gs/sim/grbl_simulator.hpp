#pragma once

// A simulated Grbl 1.1 board behind the DeviceLink interface: enough of the
// real firmware's behaviour to connect, stream jobs, jog, hold/resume, reset,
// home and zero work coordinates without hardware. Used by end-to-end tests
// and by the application's "Simulator" connection.
//
// Fidelity notes: motion is timed from distance and feed (no acceleration),
// arcs travel their chord, and the RX buffer is not modelled (lines wait for
// planner space instead). G4 and G38.x hold the input until they complete,
// as Grbl's buffer synchronisation does; probes stop where the bit touches
// one of the configured solids (a touch plate), else fail as Grbl does.

#include "gs/controller/controller.hpp"
#include "gs/protocol/types.hpp"
#include "gs/runtime/event_loop.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::sim {

struct SimAxes {
    std::array<double, 4> v{};  // x, y, z, a
    double& operator[](std::size_t i) { return v[i]; }
    double operator[](std::size_t i) const { return v[i]; }
};

// A conductive box for probing, in machine coordinates (mm).
struct Solid {
    std::array<double, 3> min{};
    std::array<double, 3> max{};
};

// A standard touch plate hooked over a stock corner (probe::Corner numbers:
// 0 bottom left, clockwise). The stock corner is at (cornerX, cornerY) with
// its top at stockTop; the plate's outer faces sit `wall` beyond the stock
// edges, its top `thickness` above the stock and its lips `depth` below it,
// and it spans `size`. With no wall and no thickness it is the stock itself.
std::vector<Solid> touchPlateOnCorner(int corner, double cornerX, double cornerY, double stockTop,
                                      double thickness = 15, double wall = 10, double size = 50,
                                      double depth = 10);

class GrblSimulator final : public controller::DeviceLink {
public:
    explicit GrblSimulator(runtime::EventLoop& loop);
    ~GrblSimulator() override;

    // Bytes the board sends to the host; always delivered from the event
    // loop, never from inside send().
    std::function<void(std::string_view bytes)> onData;

    // Power on (or DTR reset): the banner follows shortly.
    void open();
    void close();

    bool isOpen() const override { return open_; }
    void send(std::string_view bytes, controller::SendKind kind) override;

    // ---- inspection (tests) ----
    const SimAxes& machinePosition() const noexcept { return mpos_; }
    SimAxes workPosition() const;
    std::string activeState() const;
    std::size_t plannerBlocks() const noexcept { return planner_.size(); }
    const std::vector<std::string>& receivedLines() const noexcept { return received_; }
    int feedOverride() const noexcept { return overrides_[0]; }
    int spindleOverride() const noexcept { return overrides_[2]; }
    SimAxes workOffset() const;

    // ---- probing ----
    // G38.x moves stop where the bit first touches a solid. The bit is a
    // cylinder of `radius` around the programmed point (a square in XY, which
    // is exact for the axis-aligned moves probe routines make).
    void setProbeSolids(std::vector<Solid> solids) { solids_ = std::move(solids); }
    void setToolRadius(double radius) { toolRadius_ = radius; }
    bool probeTriggered() const { return touching(mpos_); }  // Pn:P

    // Motion and dwells run `factor` times faster than real time (demos,
    // application tests).
    void setSpeed(double factor) { speed_ = factor > 0 ? factor : 1.0; }

    static constexpr std::size_t kPlannerSize = 15;
    static constexpr std::int64_t kTickMs = 20;

private:
    enum class State { Idle, Run, Hold, Jog, Alarm, Home, Check };

    struct Move {
        SimAxes target;          // machine coordinates, mm
        double seconds = 0;      // time the move takes at 100 %
        double feed = 0;         // mm/min, for the status report
        bool rapid = false;      // scaled by the rapid override, not the feed one
        bool jog = false;
        bool dwell = false;      // no override scaling
        bool pause = false;      // M0/M1: hold once reached
        bool sync = false;       // G4/G38.x: input waits until this completes
        int alarm = 0;           // raised once reached (a failed probe)
        std::string after;       // output once the move completes
    };

    void emitText(std::string text);
    void banner();
    void realtime(unsigned char byte);
    void handleLine(std::string line);
    void executeSystem(const std::string& line);
    void executeGcode(const std::string& line, bool jog);
    void acceptPending();
    void enqueue(Move move);
    void enqueueProbe(const SimAxes& from, const SimAxes& target, double rate, int kind);
    SimAxes plannerEnd() const;
    void startMotion();
    void tick();
    void flushMotion();
    std::string statusReport() const;
    std::string parserState() const;
    double maxRate(std::size_t axis) const;
    bool touching(const SimAxes& at) const;
    std::optional<double> probeContact(const SimAxes& from, const SimAxes& to, bool away) const;
    std::string setting(std::string_view key) const;

    runtime::EventLoop& loop_;
    runtime::TimerScope timers_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);  // guards posted output
    bool open_ = false;
    std::string input_;                  // partial line
    std::deque<std::string> waiting_;    // lines waiting for planner space or a sync
    bool syncing_ = false;               // a G4/G38.x line holds the input
    std::vector<std::string> received_;  // every complete line, for tests

    State state_ = State::Idle;
    State beforeHold_ = State::Idle;
    SimAxes mpos_;
    std::deque<Move> planner_;
    double moveElapsed_ = 0;  // seconds into planner_.front()
    SimAxes moveStart_;
    std::int64_t lastTick_ = 0;
    runtime::TimerId tickTimer_ = 0;
    int reportCount_ = 0;

    // Modal state
    int motion_ = 0;       // 0, 1, 2, 3, 38 (probe), 80
    bool relative_ = false;
    bool inches_ = false;
    int plane_ = 17;
    int wcs_ = 0;          // 0 = G54 ... 5 = G59
    double feed_ = 0;      // mm/min
    int spindle_ = 5;      // 3, 4, 5
    double spindleSpeed_ = 0;
    bool mist_ = false;
    bool flood_ = false;
    int tool_ = 0;
    std::array<SimAxes, 6> wcsOffsets_{};
    SimAxes g92_;
    SimAxes g28_;
    SimAxes g30_;
    SimAxes probe_;
    bool probeSuccess_ = false;
    std::vector<Solid> solids_;
    double toolRadius_ = 0;
    double speed_ = 1.0;
    std::array<int, 3> overrides_{100, 100, 100};
    std::vector<std::pair<std::string, std::string>> settings_;
};

}  // namespace gs::sim
