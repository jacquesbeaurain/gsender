#include "gs/sim/grbl_simulator.hpp"

#include "gs/gcode/parser.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace gs::sim {
namespace {

constexpr double kInch = 25.4;

// Grbl 1.1 defaults for a small router; $22=1 enables homing (the simulated
// board starts unlocked, as if built without HOMING_INIT_LOCK).
const std::vector<std::pair<std::string, std::string>>& defaultSettings() {
    static const std::vector<std::pair<std::string, std::string>> settings{
        {"$0", "10"},        {"$1", "25"},         {"$2", "0"},          {"$3", "0"},          {"$4", "0"},
        {"$5", "0"},         {"$6", "0"},          {"$10", "1"},         {"$11", "0.010"},     {"$12", "0.002"},
        {"$13", "0"},        {"$20", "0"},         {"$21", "0"},         {"$22", "1"},         {"$23", "0"},
        {"$24", "25.000"},   {"$25", "500.000"},   {"$26", "250"},       {"$27", "1.000"},     {"$30", "1000"},
        {"$31", "0"},        {"$32", "0"},         {"$100", "200.000"},  {"$101", "200.000"},  {"$102", "200.000"},
        {"$110", "4000.000"}, {"$111", "4000.000"}, {"$112", "3000.000"}, {"$120", "750.000"},  {"$121", "750.000"},
        {"$122", "500.000"}, {"$130", "800.000"},  {"$131", "800.000"},  {"$132", "100.000"},
    };
    return settings;
}

std::string fixed3(double value) {
    return js::toFixed(value, 3);
}

std::string axesText(const SimAxes& axes, double scale = 1.0) {
    return fixed3(axes[0] / scale) + "," + fixed3(axes[1] / scale) + "," + fixed3(axes[2] / scale);
}

double distance(const SimAxes& a, const SimAxes& b) {
    double sum = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        sum += (a[i] - b[i]) * (a[i] - b[i]);
    }
    return std::sqrt(sum);
}

constexpr double kTouchEpsilon = 1e-6;  // mm

// The part [t0, t1] of the move from `a` to `b` (fractions of it) inside
// `solid` grown by the bit radius in XY; nullopt when the move misses it.
std::optional<std::pair<double, double>> clip(const Solid& solid, double radius, const SimAxes& a, const SimAxes& b) {
    double t0 = 0;
    double t1 = 1;
    for (std::size_t i = 0; i < 3; ++i) {
        const double grow = i < 2 ? radius : 0;
        const double lo = solid.min[i] - grow;
        const double hi = solid.max[i] + grow;
        const double d = b[i] - a[i];
        if (std::fabs(d) < 1e-12) {
            if (a[i] < lo - kTouchEpsilon || a[i] > hi + kTouchEpsilon) {
                return std::nullopt;
            }
            continue;
        }
        double ta = (lo - a[i]) / d;
        double tb = (hi - a[i]) / d;
        if (ta > tb) {
            std::swap(ta, tb);
        }
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) {
            return std::nullopt;
        }
    }
    return std::pair{t0, t1};
}

}  // namespace

std::vector<Solid> touchPlateOnCorner(int corner, double cornerX, double cornerY, double stockTop, double thickness,
                                      double wall, double size, double depth) {
    // The stock lies towards +X from a left corner and +Y from a bottom one.
    const double sx = corner == 0 || corner == 1 ? 1 : -1;
    const double sy = corner == 0 || corner == 3 ? 1 : -1;
    const double outerX = cornerX - wall * sx;
    const double outerY = cornerY - wall * sy;
    Solid plate;
    plate.min = {std::min(outerX, outerX + size * sx), std::min(outerY, outerY + size * sy), stockTop - depth};
    plate.max = {std::max(outerX, outerX + size * sx), std::max(outerY, outerY + size * sy), stockTop + thickness};
    return {plate};
}

GrblSimulator::GrblSimulator(runtime::EventLoop& loop)
    : loop_(loop), timers_(loop), settings_(defaultSettings()) {}

GrblSimulator::~GrblSimulator() {
    alive_.reset();
}

void GrblSimulator::emitText(std::string text) {
    loop_.post([token = std::weak_ptr<int>(alive_), this, text = std::move(text)] {
        if (token.lock() && open_ && onData) {
            onData(text);
        }
    });
}

// ---- power -----------------------------------------------------------------------

void GrblSimulator::open() {
    // A DTR reset reboots the board: position and modes start over, the
    // coordinate offsets (EEPROM) survive.
    open_ = true;
    timers_.clearAll();
    tickTimer_ = 0;
    planner_.clear();
    waiting_.clear();
    syncing_ = false;
    input_.clear();
    mpos_ = {};
    g92_ = {};
    state_ = State::Idle;
    motion_ = 0;
    relative_ = false;
    inches_ = false;
    plane_ = 17;
    wcs_ = 0;
    feed_ = 0;
    spindle_ = 5;
    spindleSpeed_ = 0;
    mist_ = flood_ = false;
    overrides_ = {100, 100, 100};
    timers_.timeout(100, [this] { banner(); });
}

void GrblSimulator::close() {
    open_ = false;
    timers_.clearAll();
    tickTimer_ = 0;
    planner_.clear();
    waiting_.clear();
    syncing_ = false;
}

void GrblSimulator::banner() {
    emitText("\r\nGrbl 1.1h ['$' for help]\r\n");
    if (state_ == State::Alarm) {
        emitText("[MSG:'$H'|'$X' to unlock]\r\n");
    }
}

// ---- input -----------------------------------------------------------------------

void GrblSimulator::send(std::string_view bytes, controller::SendKind) {
    if (!open_) {
        return;
    }
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);
        // Realtime commands are picked out of the stream wherever they are.
        if (byte == '?' || byte == '!' || byte == '~' || byte == 0x18 || byte >= 0x80) {
            realtime(byte);
            continue;
        }
        if (byte == '\n') {
            std::string line = std::move(input_);
            input_.clear();
            handleLine(std::move(line));
        } else if (byte != '\r') {
            input_.push_back(c);
        }
    }
}

void GrblSimulator::realtime(unsigned char byte) {
    switch (byte) {
        case '?':
            emitText(statusReport() + "\r\n");
            ++reportCount_;
            return;
        case '!':
            if (state_ == State::Jog) {
                flushMotion();  // a hold ends a jog
                state_ = State::Idle;
            } else if (state_ == State::Run) {
                beforeHold_ = State::Run;
                state_ = State::Hold;
            }
            return;
        case '~':
            if (state_ == State::Hold) {
                state_ = planner_.empty() ? State::Idle : beforeHold_;
                if (!planner_.empty()) {
                    startMotion();
                }
            }
            return;
        case 0x18: {  // soft reset
            // A reset after a completed feed hold keeps the position without an
            // alarm (why senders hold first); the simulated hold completes at once.
            const bool moving = state_ == State::Run || state_ == State::Jog || state_ == State::Home;
            flushMotion();
            waiting_.clear();
            syncing_ = false;
            input_.clear();
            timers_.clearAll();
            tickTimer_ = 0;
            g92_ = {};
            motion_ = 0;
            relative_ = false;
            inches_ = false;
            wcs_ = 0;
            spindle_ = 5;
            mist_ = flood_ = false;
            if (moving) {
                state_ = State::Alarm;
                emitText("ALARM:3\r\n");
            } else if (state_ != State::Alarm) {
                state_ = State::Idle;
            }
            banner();
            return;
        }
        case 0x85:  // jog cancel
            if (state_ == State::Jog) {
                flushMotion();
                state_ = State::Idle;
            }
            return;
        case 0x90: overrides_[0] = 100; return;
        case 0x91: overrides_[0] = std::min(overrides_[0] + 10, 200); return;
        case 0x92: overrides_[0] = std::max(overrides_[0] - 10, 10); return;
        case 0x93: overrides_[0] = std::min(overrides_[0] + 1, 200); return;
        case 0x94: overrides_[0] = std::max(overrides_[0] - 1, 10); return;
        case 0x95: overrides_[1] = 100; return;
        case 0x96: overrides_[1] = 50; return;
        case 0x97: overrides_[1] = 25; return;
        case 0x99: overrides_[2] = 100; return;
        case 0x9A: overrides_[2] = std::min(overrides_[2] + 10, 200); return;
        case 0x9B: overrides_[2] = std::max(overrides_[2] - 10, 10); return;
        case 0x9C: overrides_[2] = std::min(overrides_[2] + 1, 200); return;
        case 0x9D: overrides_[2] = std::max(overrides_[2] - 1, 10); return;
        default: return;  // door, coolant toggles, unknown bytes
    }
}

void GrblSimulator::handleLine(std::string line) {
    received_.push_back(line);
    // Lines queue behind one that waits for planner space or a sync (the
    // serial FIFO).
    if (!waiting_.empty() || syncing_ ||
        (planner_.size() >= kPlannerSize && !line.empty() && line.front() != '$')) {
        waiting_.push_back(std::move(line));
        return;
    }
    const std::string text(str::trim(line));
    if (text.empty()) {
        emitText("ok\r\n");
    } else if (text.starts_with("$J=")) {
        executeGcode(text.substr(3), true);
    } else if (text.front() == '$') {
        executeSystem(text);
    } else {
        executeGcode(text, false);
    }
}

void GrblSimulator::acceptPending() {
    while (!waiting_.empty() && !syncing_ && planner_.size() < kPlannerSize) {
        std::string line = std::move(waiting_.front());
        waiting_.pop_front();
        const std::string text(str::trim(line));
        if (text.empty()) {
            emitText("ok\r\n");
        } else if (text.starts_with("$J=")) {
            executeGcode(text.substr(3), true);
        } else if (text.front() == '$') {
            executeSystem(text);
        } else {
            executeGcode(text, false);
        }
    }
}

// ---- system commands ------------------------------------------------------------------

void GrblSimulator::executeSystem(const std::string& line) {
    const std::string command = str::toUpper(line);
    if (command == "$") {
        emitText("[HLP:$$ $# $G $I $N $x=val $Nx=line $J=line $SLP $C $X $H ~ ! ? ctrl-x]\r\nok\r\n");
        return;
    }
    if (command == "$$") {
        std::string out;
        for (const auto& [key, value] : settings_) {
            out += key + "=" + value + "\r\n";
        }
        emitText(out + "ok\r\n");
        return;
    }
    if (command == "$#") {
        std::string out;
        for (int i = 0; i < 6; ++i) {
            out += "[G" + std::to_string(54 + i) + ":" + axesText(wcsOffsets_[i]) + "]\r\n";
        }
        out += "[G28:" + axesText(g28_) + "]\r\n[G30:" + axesText(g30_) + "]\r\n[G92:" + axesText(g92_) +
               "]\r\n[TLO:0.000]\r\n[PRB:" + axesText(probe_) + ":" + (probeSuccess_ ? "1" : "0") + "]\r\n";
        emitText(out + "ok\r\n");
        return;
    }
    if (command == "$G") {
        emitText(parserState() + "\r\nok\r\n");
        return;
    }
    if (command == "$I") {
        emitText("[VER:1.1h.20190825:]\r\n[OPT:V,15,128]\r\nok\r\n");
        return;
    }
    if (command == "$N") {
        emitText("$N0=\r\n$N1=\r\nok\r\n");
        return;
    }
    if (command == "$X") {
        if (state_ == State::Alarm) {
            state_ = State::Idle;
            emitText("[MSG:Caution: Unlocked]\r\n");
        }
        emitText("ok\r\n");
        return;
    }
    // $H, or $HX/$HY/$HZ/$HA (single-axis homing, grblHAL's $22 bit 1).
    const bool singleAxis = command.size() == 3 && command.starts_with("$H") &&
                            std::string_view("XYZA").find(command[2]) != std::string_view::npos;
    if (command == "$H" || singleAxis) {
        const double homing = js::stringToNumber(setting("$22"));
        const int flags = std::isfinite(homing) ? static_cast<int>(homing) : 0;
        if ((flags & 1) == 0) {
            emitText("error:5\r\n");
            return;
        }
        const int axis = singleAxis ? static_cast<int>(std::string_view("XYZA").find(command[2])) : -1;
        if (axis >= 0 && (flags & 2) == 0) {
            emitText("error:3\r\n");
            return;
        }
        if (state_ != State::Idle && state_ != State::Alarm) {
            emitText("error:8\r\n");
            return;
        }
        state_ = State::Home;
        const double seconds = axis >= 0 ? 0.5 : 1.5;
        timers_.timeout(static_cast<std::int64_t>(seconds * 1000 / speed_), [this, axis] {
            if (axis >= 0) {
                mpos_[static_cast<std::size_t>(axis)] = 0;
            } else {
                mpos_ = {};
            }
            state_ = State::Idle;
            emitText("ok\r\n");
        });
        return;
    }
    if (command == "$C") {
        if (state_ == State::Check) {
            state_ = State::Idle;
            emitText("[MSG:Disabled]\r\nok\r\n");
            banner();  // leaving check mode resets
        } else if (state_ == State::Idle) {
            state_ = State::Check;
            emitText("[MSG:Enabled]\r\nok\r\n");
        } else {
            emitText("error:8\r\n");
        }
        return;
    }
    if (command == "$SLP") {
        emitText("ok\r\n[MSG:Sleeping]\r\n");
        return;
    }
    // $n=value
    const std::size_t eq = command.find('=');
    if (eq != std::string::npos && eq > 1) {
        const std::string key = command.substr(0, eq);
        const std::string value(str::trim(std::string_view(line).substr(eq + 1)));
        if (!std::isfinite(js::stringToNumber(value))) {
            emitText("error:2\r\n");
            return;
        }
        for (auto& [name, current] : settings_) {
            if (name == key) {
                current = value;
                emitText("ok\r\n");
                return;
            }
        }
    }
    emitText("error:3\r\n");
}

// ---- G-code ---------------------------------------------------------------------------

SimAxes GrblSimulator::workOffset() const {
    SimAxes offset;
    for (std::size_t i = 0; i < 4; ++i) {
        offset[i] = wcsOffsets_[static_cast<std::size_t>(wcs_)][i] + g92_[i];
    }
    return offset;
}

SimAxes GrblSimulator::plannerEnd() const {
    return planner_.empty() ? mpos_ : planner_.back().target;
}

double GrblSimulator::maxRate(std::size_t axis) const {
    static constexpr const char* kKeys[4] = {"$110", "$111", "$112", "$112"};
    const double rate = js::stringToNumber(setting(kKeys[axis]));
    return std::isfinite(rate) && rate > 0 ? rate : 500;
}

std::string GrblSimulator::setting(std::string_view key) const {
    for (const auto& [name, value] : settings_) {
        if (name == key) {
            return value;
        }
    }
    return {};
}

void GrblSimulator::executeGcode(const std::string& line, bool jog) {
    if (state_ == State::Alarm) {
        emitText("error:9\r\n");  // G-code locked out
        return;
    }
    if (jog && state_ != State::Idle && state_ != State::Jog) {
        emitText("error:8\r\n");
        return;
    }

    const gcode::ParsedLine parsed = gcode::parseLine(line);
    std::optional<int> motion;
    int probeKind = 382;  // G38.2 .. G38.5
    bool dwell = false;
    bool g53 = false;
    std::optional<int> g10;
    std::optional<int> home;  // 28 / 30
    bool storeHome = false;
    bool g92 = false;
    bool g92Clear = false;
    bool pause = false;
    bool programEnd = false;
    // Jogs carry their own distance and unit modes.
    bool relative = relative_;
    bool inches = inches_;
    std::array<std::optional<double>, 4> axes;
    std::optional<double> feed, speed, p, l, tool;

    for (const gcode::Word& word : parsed.words) {
        const double v = word.value;
        if (!word.isNumeric()) {
            emitText("error:2\r\n");
            return;
        }
        switch (word.letter) {
            case 'G': {
                const int code = static_cast<int>(std::lround(v * 10));  // 38.2 -> 382
                if (code == 0 || code == 10 || code == 20 || code == 30) motion = code / 10;
                else if (code >= 382 && code <= 385) { motion = 38; probeKind = code; }
                else if (code == 40) dwell = true;
                else if (code == 100) g10 = 10;
                else if (code == 170 || code == 180 || code == 190) plane_ = code / 10;
                else if (code == 200) inches = true;
                else if (code == 210) inches = false;
                else if (code == 280 || code == 300) home = code / 10;
                else if (code == 281 || code == 301) { home = code / 10; storeHome = true; }
                else if (code == 530) g53 = true;
                else if (code >= 540 && code <= 590 && code % 10 == 0) wcs_ = (code - 540) / 10;
                else if (code == 800) motion = 80;
                else if (code == 900) relative = false;
                else if (code == 910) relative = true;
                else if (code == 920) g92 = true;
                else if (code == 921) g92Clear = true;
                else if (code == 911 || code == 901 || code == 930 || code == 940 || code == 400 || code == 431 ||
                         code == 490 || code == 610) {
                    // accepted, no effect in the simulation
                } else {
                    emitText("error:20\r\n");
                    return;
                }
                break;
            }
            case 'M': {
                const int code = static_cast<int>(std::lround(v));
                if (code == 0 || code == 1) pause = true;
                else if (code == 2 || code == 30) programEnd = true;
                else if (code == 3 || code == 4 || code == 5) spindle_ = code;
                else if (code == 6) { /* tool change: nothing to do */ }
                else if (code == 7) mist_ = true;
                else if (code == 8) flood_ = true;
                else if (code == 9) mist_ = flood_ = false;
                else {
                    emitText("error:20\r\n");
                    return;
                }
                break;
            }
            case 'X': axes[0] = v; break;
            case 'Y': axes[1] = v; break;
            case 'Z': axes[2] = v; break;
            case 'A': axes[3] = v; break;
            case 'F': feed = v; break;
            case 'S': speed = v; break;
            case 'P': p = v; break;
            case 'L': l = v; break;
            case 'T': tool = v; break;
            case 'I': case 'J': case 'K': case 'R': case 'N': break;
            default:
                emitText("error:20\r\n");
                return;
        }
    }

    if (!jog) {
        relative_ = relative;
        inches_ = inches;
        if (motion) {
            motion_ = *motion;
        }
    }
    const double scale = inches ? kInch : 1.0;
    if (feed) {
        const double mmPerMin = *feed * scale;
        if (!jog) {
            feed_ = mmPerMin;
        }
    }
    if (speed) {
        spindleSpeed_ = *speed;
    }
    if (tool) {
        tool_ = static_cast<int>(*tool);
    }

    const SimAxes end = plannerEnd();
    const auto targetFor = [&](bool machine) {
        SimAxes target = end;
        const SimAxes offset = workOffset();
        for (std::size_t i = 0; i < 4; ++i) {
            if (!axes[i]) {
                continue;
            }
            const double value = i < 3 ? *axes[i] * scale : *axes[i];
            if (machine) {
                target[i] = value;
            } else if (relative) {
                target[i] = end[i] + value;
            } else {
                target[i] = value + offset[i];
            }
        }
        return target;
    };
    const bool anyAxis = std::any_of(axes.begin(), axes.end(), [](const auto& a) { return a.has_value(); });
    const bool check = state_ == State::Check;

    bool deferOk = false;  // answered once a synchronous command completes
    if (dwell && !check) {
        // Grbl empties the planner, dwells, then answers.
        Move wait{end, std::max(p.value_or(0), 0.0), 0, false, false, true};
        wait.sync = true;
        wait.after = "ok\r\n";
        enqueue(std::move(wait));
        deferOk = true;
    }
    if (g10) {
        const int index = static_cast<int>(p.value_or(0));
        const std::size_t slot = index == 0 ? static_cast<std::size_t>(wcs_) : static_cast<std::size_t>(index - 1);
        if (slot < 6 && (l == 2.0 || l == 20.0)) {
            for (std::size_t i = 0; i < 4; ++i) {
                if (!axes[i]) {
                    continue;
                }
                const double value = i < 3 ? *axes[i] * scale : *axes[i];
                // L2: the offset itself; L20: make the current position read `value`.
                wcsOffsets_[slot][i] = l == 2.0 ? value : end[i] - g92_[i] - value;
            }
        } else {
            emitText("error:20\r\n");
            return;
        }
    } else if (home) {
        SimAxes& stored = *home == 28 ? g28_ : g30_;
        if (storeHome) {
            stored = end;
        } else if (!check) {
            if (anyAxis) {
                const SimAxes via = targetFor(false);
                enqueue(Move{via, distance(end, via) / (maxRate(0) / 60), maxRate(0), true});
            }
            const SimAxes from = plannerEnd();
            enqueue(Move{stored, distance(from, stored) / (maxRate(0) / 60), maxRate(0), true});
        }
    } else if (g92) {
        for (std::size_t i = 0; i < 4; ++i) {
            if (axes[i]) {
                const double value = i < 3 ? *axes[i] * scale : *axes[i];
                g92_[i] = end[i] - wcsOffsets_[static_cast<std::size_t>(wcs_)][i] - value;
            }
        }
    } else if (g92Clear) {
        g92_ = {};
    } else if (anyAxis) {
        const int mode = motion.value_or(jog ? 1 : motion_);
        if (mode == 80) {
            emitText("error:31\r\n");  // axis words with no motion mode
            return;
        }
        const SimAxes target = targetFor(g53);
        const double length = distance(end, target);
        if (mode == 0) {
            double rate = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < 4; ++i) {
                if (target[i] != end[i]) {
                    rate = std::min(rate, maxRate(i));
                }
            }
            if (!std::isfinite(rate)) {
                rate = maxRate(0);
            }
            if (!check) {
                enqueue(Move{target, length / (rate / 60), rate, true});
            }
        } else {
            const double rate = jog ? feed.value_or(0) * scale : feed_;
            if (!(rate > 0)) {
                emitText("error:22\r\n");  // undefined feed rate
                return;
            }
            if (!check && mode == 38 && !jog) {
                enqueueProbe(end, target, rate, probeKind);
                deferOk = true;
            } else if (!check) {
                Move move{target, length / (rate / 60), rate};
                move.jog = jog;
                enqueue(std::move(move));
            }
        }
    }
    if (pause && !check) {
        Move marker{plannerEnd(), 0, 0, false, false, true};
        marker.pause = true;
        enqueue(std::move(marker));
    }
    if (programEnd) {
        // Program end restores the default modes.
        motion_ = 1;
        relative_ = false;
        wcs_ = 0;
        spindle_ = 5;
        mist_ = flood_ = false;
        g92_ = {};
        if (!check) {
            Move marker{plannerEnd(), 0, 0, false, false, true};
            marker.after = "[MSG:Pgm End]\r\n";
            enqueue(std::move(marker));
        }
    }
    if (!deferOk) {
        emitText("ok\r\n");
    }
}

// Grbl's mc_probe_cycle(): after the planner empties, a probe that starts in
// the wrong pin state alarms at once; otherwise the bit moves until the pin
// changes. G38.2/G38.4 alarm when it never does; G38.3/G38.5 just report.
void GrblSimulator::enqueueProbe(const SimAxes& from, const SimAxes& target, double rate, int kind) {
    const bool away = kind >= 384;
    const bool reportOnly = kind == 383 || kind == 385;
    Move move{from, 0, rate};
    move.sync = true;
    if (touching(from) != away) {
        move.alarm = 4;
        move.after = "ok\r\n";
        enqueue(std::move(move));
        return;
    }
    const std::optional<double> t = probeContact(from, target, away);
    SimAxes stop = target;
    if (t) {
        for (std::size_t i = 0; i < 4; ++i) {
            stop[i] = from[i] + (target[i] - from[i]) * *t;
        }
    }
    move.target = stop;
    move.seconds = distance(from, stop) / (rate / 60);
    if (t || reportOnly) {
        probe_ = stop;
        probeSuccess_ = t.has_value();
    } else {
        probeSuccess_ = false;
        move.alarm = 5;
    }
    move.after = "[PRB:" + axesText(probe_) + ":" + (probeSuccess_ ? "1" : "0") + "]\r\nok\r\n";
    enqueue(std::move(move));
}

bool GrblSimulator::touching(const SimAxes& at) const {
    return std::any_of(solids_.begin(), solids_.end(),
                       [&](const Solid& solid) { return clip(solid, toolRadius_, at, at).has_value(); });
}

std::optional<double> GrblSimulator::probeContact(const SimAxes& from, const SimAxes& to, bool away) const {
    std::optional<double> best;
    for (const Solid& solid : solids_) {
        const auto inside = clip(solid, toolRadius_, from, to);
        if (!inside) {
            continue;
        }
        // Towards: where the bit first enters a solid. Away: where it leaves
        // the one it started in.
        const double t = away ? inside->second : inside->first;
        if (away && (inside->first > kTouchEpsilon || t >= 1)) {
            continue;
        }
        if (!best || (away ? t > *best : t < *best)) {
            best = t;
        }
    }
    return best;
}

// ---- motion ---------------------------------------------------------------------------

void GrblSimulator::enqueue(Move move) {
    if (planner_.empty()) {
        moveStart_ = mpos_;
        moveElapsed_ = 0;
    }
    const bool jogMove = move.jog;
    syncing_ = syncing_ || move.sync;
    planner_.push_back(std::move(move));
    if (state_ == State::Idle) {
        state_ = jogMove ? State::Jog : State::Run;
    }
    startMotion();
}

void GrblSimulator::startMotion() {
    if (tickTimer_ == 0) {
        lastTick_ = loop_.nowMs();
        tickTimer_ = timers_.interval(kTickMs, [this] { tick(); });
    }
}

void GrblSimulator::flushMotion() {
    planner_.clear();
    moveElapsed_ = 0;
    moveStart_ = mpos_;
    timers_.clear(tickTimer_);
}

void GrblSimulator::tick() {
    const std::int64_t now = loop_.nowMs();
    double dt = static_cast<double>(now - lastTick_) / 1000.0 * speed_;
    lastTick_ = now;
    if (state_ == State::Hold || state_ == State::Alarm || state_ == State::Home) {
        return;
    }
    while (dt > 0 && !planner_.empty()) {
        Move& move = planner_.front();
        const int percent = move.dwell || move.jog ? 100 : (move.rapid ? overrides_[1] : overrides_[0]);
        const double rate = percent / 100.0;
        const double left = (move.seconds - moveElapsed_) / rate;  // wall seconds
        if (dt < left) {
            moveElapsed_ += dt * rate;
            const double t = move.seconds > 0 ? moveElapsed_ / move.seconds : 1.0;
            for (std::size_t i = 0; i < 4; ++i) {
                mpos_[i] = moveStart_[i] + (move.target[i] - moveStart_[i]) * t;
            }
            dt = 0;
            break;
        }
        dt -= std::max(left, 0.0);
        mpos_ = move.target;
        const bool pause = move.pause;
        if (move.alarm != 0) {
            state_ = State::Alarm;
            emitText("ALARM:" + std::to_string(move.alarm) + "\r\n");
        }
        if (!move.after.empty()) {
            emitText(move.after);
        }
        if (move.sync) {
            syncing_ = false;
        }
        planner_.pop_front();
        moveElapsed_ = 0;
        moveStart_ = mpos_;
        acceptPending();
        if (pause) {
            beforeHold_ = State::Run;
            state_ = State::Hold;
            break;
        }
    }
    if (planner_.empty() && (state_ == State::Run || state_ == State::Jog)) {
        state_ = State::Idle;
        timers_.clear(tickTimer_);
    }
}

// ---- reports ----------------------------------------------------------------------------

SimAxes GrblSimulator::workPosition() const {
    const SimAxes offset = workOffset();
    SimAxes work;
    for (std::size_t i = 0; i < 4; ++i) {
        work[i] = mpos_[i] - offset[i];
    }
    return work;
}

std::string GrblSimulator::activeState() const {
    switch (state_) {
        case State::Idle: return "Idle";
        case State::Run: return "Run";
        case State::Hold: return "Hold:0";
        case State::Jog: return "Jog";
        case State::Alarm: return "Alarm";
        case State::Home: return "Home";
        case State::Check: return "Check";
    }
    return "Idle";
}

std::string GrblSimulator::statusReport() const {
    const double mask = js::stringToNumber(setting("$10"));
    const int flags = std::isfinite(mask) ? static_cast<int>(mask) : 1;
    const double units = setting("$13") == "1" ? kInch : 1.0;
    std::string report = "<" + activeState();
    if (flags & 1) {
        report += "|MPos:" + axesText(mpos_, units);
    } else {
        report += "|WPos:" + axesText(workPosition(), units);
    }
    if (flags & 2) {
        report += "|Bf:" + std::to_string(kPlannerSize - std::min(planner_.size(), kPlannerSize)) + ",128";
    }
    const bool moving = !planner_.empty() && (state_ == State::Run || state_ == State::Jog);
    const double feed = moving ? planner_.front().feed : 0;
    const double spindle = spindle_ == 5 ? 0 : spindleSpeed_;
    report += "|FS:" + js::numberToString(std::round(feed / units)) + "," + js::numberToString(spindle);
    if (probeTriggered()) {
        report += "|Pn:P";
    }
    report += "|Ov:" + std::to_string(overrides_[0]) + "," + std::to_string(overrides_[1]) + "," +
              std::to_string(overrides_[2]);
    report += "|WCO:" + axesText(workOffset(), units) + ">";
    return report;
}

std::string GrblSimulator::parserState() const {
    std::string coolant;
    if (mist_) {
        coolant += " M7";
    }
    if (flood_) {
        coolant += " M8";
    }
    if (coolant.empty()) {
        coolant = " M9";
    }
    return "[GC:G" + std::to_string(motion_ == 38 ? 1 : motion_) + " G" + std::to_string(54 + wcs_) + " G" +
           std::to_string(plane_) + (inches_ ? " G20" : " G21") + (relative_ ? " G91" : " G90") + " G94 M" +
           std::to_string(spindle_) + coolant + " T" + std::to_string(tool_) + " F" +
           js::numberToString(std::round(feed_ / (inches_ ? kInch : 1.0))) + " S" + js::numberToString(spindleSpeed_) +
           "]";
}

}  // namespace gs::sim
