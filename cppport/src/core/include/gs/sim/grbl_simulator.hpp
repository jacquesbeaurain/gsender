#pragma once

// A simulated Grbl 1.1 board behind the DeviceLink interface: enough of the
// real firmware's behaviour to connect, stream jobs, jog, hold/resume, reset,
// home and zero work coordinates without hardware. Used by end-to-end tests
// and by the application's "Simulator" connection.
//
// Fidelity notes: motion is timed from distance and feed (no acceleration),
// arcs travel their chord, probing always succeeds at the end of the move,
// and the RX buffer is not modelled (lines wait for planner space instead).

#include "gs/controller/controller.hpp"
#include "gs/protocol/types.hpp"
#include "gs/runtime/event_loop.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gs::sim {

struct SimAxes {
    std::array<double, 4> v{};  // x, y, z, a
    double& operator[](std::size_t i) { return v[i]; }
    double operator[](std::size_t i) const { return v[i]; }
};

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
    SimAxes plannerEnd() const;
    void startMotion();
    void tick();
    void flushMotion();
    std::string statusReport() const;
    std::string parserState() const;
    SimAxes workOffset() const;
    double maxRate(std::size_t axis) const;
    std::string setting(std::string_view key) const;

    runtime::EventLoop& loop_;
    runtime::TimerScope timers_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);  // guards posted output
    bool open_ = false;
    std::string input_;                  // partial line
    std::deque<std::string> waiting_;    // lines waiting for planner space
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
    std::array<int, 3> overrides_{100, 100, 100};
    std::vector<std::pair<std::string, std::string>> settings_;
};

}  // namespace gs::sim
