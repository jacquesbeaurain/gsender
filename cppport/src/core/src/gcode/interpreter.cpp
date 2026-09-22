#include "gs/gcode/interpreter.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace gs::gcode {
namespace {

constexpr double kInchToMm = 25.4;
constexpr double kRotaryCurveThreshold = 30;  // degrees of A travel drawn as a curve
constexpr double kAtcToolChangeSeconds = 45;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

constexpr std::uint32_t bitFor(char letter) noexcept {
    return 1u << (letter - 'A');
}

constexpr std::uint32_t kAxisLetterMask = bitFor('X') | bitFor('Y') | bitFor('Z') | bitFor('A') |
                                          bitFor('B') | bitFor('C') | bitFor('I') | bitFor('J') |
                                          bitFor('K');

bool isAxisLetter(char letter) noexcept {
    return letter >= 'A' && letter <= 'Z' && (kAxisLetterMask & bitFor(letter)) != 0;
}

// Point on the arc (ThreeJS EllipseCurve semantics) at parameter t in [0, 1].
double arcSweep(double startAngle, double endAngle, bool clockwise) {
    constexpr double kTwoPi = 2 * std::numbers::pi;
    constexpr double kEpsilon = std::numeric_limits<double>::epsilon();
    double delta = endAngle - startAngle;
    const bool samePoints = std::fabs(delta) < kEpsilon;
    while (delta < 0) {
        delta += kTwoPi;
    }
    while (delta > kTwoPi) {
        delta -= kTwoPi;
    }
    if (delta < kEpsilon) {
        delta = samePoints ? 0 : kTwoPi;
    }
    if (clockwise && !samePoints) {
        delta = (delta == kTwoPi) ? -kTwoPi : delta - kTwoPi;
    }
    return delta;
}

}  // namespace

// ---- OrderedStringSet ------------------------------------------------------

void OrderedStringSet::add(std::string value) {
    if (index_.insert(value).second) {
        values_.push_back(std::move(value));
    }
}

// ---- Args ------------------------------------------------------------------

bool Interpreter::Args::has(char letter) const noexcept {
    return letter >= 'A' && letter <= 'Z' && (mask & bitFor(letter)) != 0;
}

std::string_view Interpreter::Args::get(char letter) const noexcept {
    return has(letter) ? values[static_cast<std::size_t>(letter - 'A')] : std::string_view{};
}

void Interpreter::Args::set(char letter, std::string_view value) noexcept {
    if (letter < 'A' || letter > 'Z') {
        return;
    }
    values[static_cast<std::size_t>(letter - 'A')] = value;
    mask |= bitFor(letter);
}

double Interpreter::Args::number(char letter) const {
    return has(letter) ? js::stringToNumber(get(letter)) : kNaN;
}

// ---- Interpreter -------------------------------------------------------------

Interpreter::Interpreter(InterpreterOptions options) : options_(options) {
    const auto sanitize = [](AxisLimits& limits, const AxisLimits& fallback) {
        if (!(limits.acceleration > 0)) {
            limits.acceleration = fallback.acceleration;
        }
        if (!(limits.maxFeed > 0)) {
            limits.maxFeed = fallback.maxFeed;
        }
    };
    const InterpreterOptions defaults;
    sanitize(options_.x, defaults.x);
    sanitize(options_.y, defaults.y);
    sanitize(options_.z, defaults.z);
    sanitize(options_.a, defaults.a);

    if (options_.rotaryDiameter && *options_.rotaryDiameter > 0) {
        rotaryDiameter_ = *options_.rotaryDiameter;
        autoDetectDiameter_ = false;
    } else {
        autoDetectDiameter_ = options_.autoDetectRotaryDiameter;
    }
}

void Interpreter::processProgram(std::string_view text) {
    for (std::string_view line : str::splitLines(text)) {
        processLine(line);
    }
}

void Interpreter::processLine(std::string_view line) {
    lineHadTokens_ = false;
    lineTime_ = 0;
    lineSpindle_.reset();
    ++totalLines_;

    if (line.empty()) {
        scan_.tokens.clear();
        return;
    }
    scanLine(line, scan_);
    if (scan_.tokens.empty()) {
        return;
    }
    lineHadTokens_ = true;
    if (scan_.hasInvalidTokens) {
        invalidLines_.emplace_back(line);
    }

    // Feed and spindle words apply to the whole line.
    for (const Token& token : scan_.tokens) {
        if (token.letter == 'F') {
            const double value = js::stringToNumber(token.value);
            if (std::isfinite(value)) {
                feed_ = value;
            }
            feedrates_.add("F" + std::string(token.value));
        } else if (token.letter == 'S') {
            spindleSpeeds_.add("S" + std::string(token.value));
            const double speed = js::stringToNumber(token.value);
            recordEvent('S', speed);
            if (!std::isnan(speed)) {
                lineSpindle_ = speed;
            }
        }
    }

    // Each G, M or T word starts a command group; other words attach to the
    // group before them.
    std::size_t groupStart = 0;
    const std::size_t count = scan_.tokens.size();
    for (std::size_t i = 0; i < count; ++i) {
        const char letter = scan_.tokens[i].letter;
        if ((letter == 'G' || letter == 'M' || letter == 'T') && i > groupStart) {
            dispatchGroup(groupStart, i);
            groupStart = i;
        }
    }
    dispatchGroup(groupStart, count);

    auto event = events_.find(totalLines_);
    if (event != events_.end() && (event->second.T || event->second.M)) {
        std::string comment = extractComments(line);
        if (!comment.empty()) {
            event->second.comment = std::move(comment);
        }
    }
}

void Interpreter::dispatchGroup(std::size_t begin, std::size_t end) {
    if (end <= begin) {
        return;
    }
    const Token& lead = scan_.tokens[begin];
    const double code = js::stringToNumber(lead.value);

    switch (lead.letter) {
        case 'G': {
            Args args;
            for (std::size_t i = begin + 1; i < end; ++i) {
                args.set(scan_.tokens[i].letter, scan_.tokens[i].value);
            }
            noteUsedAxes(args);
            const Cmd cmd = gCommand(code);
            if (isMotion(cmd)) {
                motionMode_ = cmd;
            } else if (cmd == Cmd::G80) {
                motionMode_ = Cmd::None;
            }

            // Axis words on a non-motion G block ("G90 X10") still move the
            // machine in the active motion mode - except for commands that
            // consume axis words themselves.
            const bool consumesAxes = code == 10 || code == 43.1 || code == 92;
            if ((args.mask & kAxisLetterMask) != 0 && !isMotion(cmd) && !consumesAxes) {
                Args modalArgs;
                Args motionArgs;
                for (char letter = 'A'; letter <= 'Z'; ++letter) {
                    if (!args.has(letter)) {
                        continue;
                    }
                    (isAxisLetter(letter) ? motionArgs : modalArgs).set(letter, args.get(letter));
                }
                execute(cmd, modalArgs);
                if (motionArgs.mask != 0 && motionMode_ != Cmd::None) {
                    execute(motionMode_, motionArgs);
                }
                return;
            }
            execute(cmd, args);
            return;
        }
        case 'M': {
            Args args;
            for (std::size_t i = begin + 1; i < end; ++i) {
                args.set(scan_.tokens[i].letter, scan_.tokens[i].value);
            }
            recordEvent('M', code);
            execute(mCommand(code), args);
            return;
        }
        case 'T':
            recordEvent('T', code);
            setTool(lead.value);
            return;
        case 'S':
            recordEvent('S', code);
            return;
        default:
            break;
    }

    if (isAxisLetter(lead.letter) && motionMode_ != Cmd::None) {
        Args args;
        for (std::size_t i = begin; i < end; ++i) {
            args.set(scan_.tokens[i].letter, scan_.tokens[i].value);
        }
        noteUsedAxes(args);
        execute(motionMode_, args);
    }
}

void Interpreter::execute(Cmd cmd, const Args& args) {
    switch (cmd) {
        case Cmd::G0:
        case Cmd::G1:
            linearMove(cmd, args);
            break;
        case Cmd::G2:
        case Cmd::G3:
            arcMove(cmd, args);
            break;
        case Cmd::G4:
            dwell(args);
            break;
        case Cmd::G17:
        case Cmd::G18:
        case Cmd::G19:
            modal_.plane = commandName(cmd);
            break;
        case Cmd::G20:
        case Cmd::G21:
            modal_.units = commandName(cmd);
            break;
        case Cmd::G38_2:
        case Cmd::G38_3:
        case Cmd::G38_4:
        case Cmd::G38_5:
        case Cmd::G80:
            modal_.motion = commandName(cmd);
            break;
        case Cmd::G43_1:
        case Cmd::G49:
            modal_.tlo = commandName(cmd);
            break;
        case Cmd::G54:
        case Cmd::G55:
        case Cmd::G56:
        case Cmd::G57:
        case Cmd::G58:
        case Cmd::G59:
            modal_.wcs = commandName(cmd);
            break;
        case Cmd::G90:
        case Cmd::G91:
            modal_.distance = commandName(cmd);
            break;
        case Cmd::G92:
            setG92(args);
            break;
        case Cmd::G92_1:
            clearG92();
            break;
        case Cmd::G93:
        case Cmd::G94:
        case Cmd::G95:
            modal_.feedrate = commandName(cmd);
            break;
        case Cmd::M0:
        case Cmd::M1:
        case Cmd::M2:
        case Cmd::M30:
            modal_.program = commandName(cmd);
            break;
        case Cmd::M3:
        case Cmd::M4:
        case Cmd::M5:
            modal_.spindle = commandName(cmd);
            break;
        case Cmd::M6:
            sawM6_ = true;
            toolChangeLines_.push_back(totalLines_);
            if (args.has('T')) {
                setTool(args.get('T'));
            }
            if (options_.atcEnabled) {
                addTime(kAtcToolChangeSeconds);
            }
            break;
        case Cmd::M7:
            if (modal_.coolant.find("M7") == std::string::npos) {
                modal_.coolant = modal_.coolant.find("M8") != std::string::npos ? "M7,M8" : "M7";
            }
            break;
        case Cmd::M8:
            if (modal_.coolant.find("M8") == std::string::npos) {
                modal_.coolant = modal_.coolant.find("M7") != std::string::npos ? "M7,M8" : "M8";
            }
            break;
        case Cmd::M9:
            modal_.coolant = "M9";
            break;
        case Cmd::G10:
        case Cmd::None:
            break;
    }
}

double Interpreter::translate(double current, double value, bool relative, bool linear) const {
    if (std::isnan(value)) {
        return current;
    }
    if (linear && modal_.units == "G20") {
        value *= kInchToMm;
    }
    return relative ? current + value : value;
}

double Interpreter::translateAxis(double current, const Args& args, char letter) const {
    if (!args.has(letter)) {
        return current;
    }
    return translate(current, args.number(letter), modal_.distance == "G91", letter != 'A');
}

Vec4 Interpreter::withOffsets(const Vec4& v) const noexcept {
    return {v.x + offsets_.x, v.y + offsets_.y, v.z + offsets_.z, v.a + offsets_.a};
}

void Interpreter::linearMove(Cmd cmd, const Args& args) {
    modal_.motion = commandName(cmd);

    const Vec4 from = position_;
    const Vec4 to{translateAxis(position_.x, args, 'X'), translateAxis(position_.y, args, 'Y'),
                  translateAxis(position_.z, args, 'Z'), translateAxis(position_.a, args, 'A')};

    if (sink_) {
        const bool curved = from.a != to.a && std::fabs(to.a - from.a) > kRotaryCurveThreshold;
        if (curved) {
            sink_->addCurve(modal_, withOffsets(from), withOffsets(to));
        } else {
            sink_->addLine(modal_, withOffsets(from), withOffsets(to));
        }
    }
    addMoveTime(from, to);
    updateBounds(withOffsets(to));
    position_ = to;
}

void Interpreter::arcMove(Cmd cmd, const Args& args) {
    const bool clockwise = cmd == Cmd::G2;
    modal_.motion = commandName(cmd);

    const Vec4 target{translateAxis(position_.x, args, 'X'), translateAxis(position_.y, args, 'Y'),
                      translateAxis(position_.z, args, 'Z'), translateAxis(position_.a, args, 'A')};
    // IJK are always relative to the start point (G91.1).
    const Vec4 center{translate(position_.x, args.number('I'), true, true),
                      translate(position_.y, args.number('J'), true, true),
                      translate(position_.z, args.number('K'), true, true), position_.a};

    // Rotate into the arc plane: x/y in-plane, z along the plane normal.
    const auto toPlane = [this](const Vec4& v) -> Vec4 {
        if (modal_.plane == "G18") {
            return {v.z, v.x, v.y, v.a};
        }
        if (modal_.plane == "G19") {
            return {v.y, v.z, v.x, v.a};
        }
        return v;
    };
    const auto fromPlane = [this](const Vec4& v) -> Vec4 {
        if (modal_.plane == "G18") {
            return {v.y, v.z, v.x, v.a};
        }
        if (modal_.plane == "G19") {
            return {v.z, v.x, v.y, v.a};
        }
        return v;
    };

    const Vec4 p1 = toPlane(withOffsets(position_));
    const Vec4 p2 = toPlane(withOffsets(target));
    Vec4 p0 = toPlane(withOffsets(center));

    if (args.has('R')) {
        double radius = args.number('R');
        if (!std::isnan(radius) && radius != 0) {
            if (modal_.units == "G20") {
                radius *= kInchToMm;
            }
            const double x = p2.x - p1.x;
            const double y = p2.y - p1.y;
            const double distance = std::hypot(x, y);
            double height = std::sqrt(std::max(0.0, 4 * radius * radius - x * x - y * y)) / 2;
            if (clockwise) {
                height = -height;
            }
            if (radius < 0) {
                height = -height;
            }
            if (distance > 0) {
                p0.x = p1.x + x / 2 - (y / distance) * height;
                p0.y = p1.y + y / 2 + (x / distance) * height;
            }
        }
    }

    if (sink_) {
        sink_->addArc(modal_, p1, p2, p0);
    }

    // Time and bounds follow the actual arc rather than its chord.
    const double radius = std::hypot(p1.x - p0.x, p1.y - p0.y);
    const double startAngle = std::atan2(p1.y - p0.y, p1.x - p0.x);
    double endAngle = std::atan2(p2.y - p0.y, p2.x - p0.x);
    if (startAngle == endAngle) {
        endAngle += 2 * std::numbers::pi;  // full circle
    }
    const double sweep = arcSweep(startAngle, endAngle, clockwise);
    const double arcLength = std::hypot(std::fabs(sweep) * radius, p2.z - p1.z);

    {
        const AxisLimits& inPlane1 = modal_.plane == "G18" ? options_.z : (modal_.plane == "G19" ? options_.y : options_.x);
        const AxisLimits& inPlane2 = modal_.plane == "G18" ? options_.x : (modal_.plane == "G19" ? options_.z : options_.y);
        double maxFeed = std::min(inPlane1.maxFeed, inPlane2.maxFeed);
        double acceleration = std::min(inPlane1.acceleration, inPlane2.acceleration);
        if (p2.z != p1.z) {
            const AxisLimits& normal =
                modal_.plane == "G18" ? options_.y : (modal_.plane == "G19" ? options_.x : options_.z);
            maxFeed = std::min(maxFeed, normal.maxFeed);
            acceleration = std::min(acceleration, normal.acceleration);
        }
        double feed = modal_.units == "G20" ? feed_ * kInchToMm : feed_;
        feed = std::min(feed, maxFeed);
        const double f = feed / 60;
        double duration = 0;
        if (f == lastF_) {
            duration = f != 0 ? arcLength / f : 0;
        } else {
            duration = acceleratedMoveTime(arcLength, f, acceleration);
        }
        lastF_ = f;
        addTime(duration);
    }

    // Include the arc's extreme points, not just its end point.
    updateBounds(fromPlane(p2));
    if (radius > 0) {
        const double lo = std::min(startAngle, startAngle + sweep);
        const double hi = std::max(startAngle, startAngle + sweep);
        for (int quarter = -8; quarter <= 8; ++quarter) {
            const double angle = quarter * std::numbers::pi / 2;
            if (angle > lo && angle < hi) {
                const double t = (angle - startAngle) / sweep;
                Vec4 extreme{p0.x + radius * std::cos(angle), p0.y + radius * std::sin(angle),
                             p1.z + (p2.z - p1.z) * t, p2.a};
                updateBounds(fromPlane(extreme));
            }
        }
    }

    position_ = target;
}

void Interpreter::dwell(const Args& args) {
    double seconds = 0;
    const double p = args.number('P');
    if (std::isfinite(p) && p > 0) {
        seconds = p;
    }
    const double s = args.number('S');
    if (std::isfinite(s) && s > 0) {
        seconds = s;
    }
    addTime(seconds);
}

void Interpreter::setG92(const Args& args) {
    if (!args.has('X') && !args.has('Y') && !args.has('Z') && !args.has('A')) {
        clearG92();
        return;
    }
    const auto apply = [&](double& position, double& offset, char letter) {
        if (!args.has(letter)) {
            return;
        }
        const double value = translate(position, args.number(letter), false, letter != 'A');
        offset += position - value;
        position = value;
    };
    apply(position_.x, offsets_.x, 'X');
    apply(position_.y, offsets_.y, 'Y');
    apply(position_.z, offsets_.z, 'Z');
    apply(position_.a, offsets_.a, 'A');
}

void Interpreter::clearG92() {
    position_.x += offsets_.x;
    position_.y += offsets_.y;
    position_.z += offsets_.z;
    position_.a += offsets_.a;
    offsets_ = {};
}

void Interpreter::setTool(std::string_view code) {
    const double number = js::stringToNumber(code);
    const std::string normalized = std::isfinite(number) ? js::numberToString(number) : std::string(code);
    if (std::isfinite(number)) {
        modal_.tool = number;
    }
    tools_.add("T" + normalized);
}

void Interpreter::noteUsedAxes(const Args& args) {
    if (args.has('X')) {
        usedAxes_ |= 1;
    }
    if (args.has('Y')) {
        usedAxes_ |= 2;
    }
    if (args.has('Z')) {
        usedAxes_ |= 4;
    }
    if (args.has('A')) {
        usedAxes_ |= 8;
    }
}

void Interpreter::recordEvent(char kind, double value) {
    SpindleToolEvent& event = events_[totalLines_];
    switch (kind) {
        case 'S':
            event.S = value;
            break;
        case 'T':
            event.T = value;
            break;
        case 'M':
            event.M = value;
            break;
        default:
            break;
    }
}

void Interpreter::updateBounds(const Vec4& p) {
    const std::array<double, 4> values{p.x, p.y, p.z, p.a};
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (std::isnan(values[i])) {
            continue;
        }
        maxBounds_[i] = std::max(maxBounds_[i], values[i]);
        minBounds_[i] = std::min(minBounds_[i], values[i]);
    }
    // Rotary jobs: estimate the stock diameter from the Y/Z extents.
    if (autoDetectDiameter_ && p.a != 0) {
        const double detected = std::max(maxBounds_[1] - minBounds_[1], maxBounds_[2] - minBounds_[2]);
        if (detected > rotaryDiameter_) {
            rotaryDiameter_ = std::max(detected, 10.0);
        }
    }
}

BoundingBox Interpreter::bounds() const noexcept {
    return {{minBounds_[0], minBounds_[1], minBounds_[2], minBounds_[3]},
            {maxBounds_[0], maxBounds_[1], maxBounds_[2], maxBounds_[3]}};
}

std::string Interpreter::usedAxes() const {
    std::string axes;
    if (usedAxes_ & 1) {
        axes += 'X';
    }
    if (usedAxes_ & 2) {
        axes += 'Y';
    }
    if (usedAxes_ & 4) {
        axes += 'Z';
    }
    if (usedAxes_ & 8) {
        axes += 'A';
    }
    return axes;
}

FileType Interpreter::fileType() const noexcept {
    const bool usesY = (usedAxes_ & 2) != 0;
    const bool usesA = (usedAxes_ & 8) != 0;
    if (usesY && usesA) {
        return FileType::FourAxis;
    }
    return usesA ? FileType::Rotary : FileType::Default;
}

void Interpreter::addTime(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) {
        return;
    }
    totalTime_ += seconds;
    lineTime_ += seconds;
}

// Port of GCodeVirtualizer.calculateMachiningTime().
void Interpreter::addMoveTime(const Vec4& from, const Vec4& to) {
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double dz = to.z - from.z;
    const double da = to.a - from.a;

    const double travelXY = std::hypot(dx, dy);
    if (std::isnan(travelXY)) {
        return;
    }
    const double linearTravel = std::hypot(travelXY, dz);
    const double circumference = std::numbers::pi * rotaryDiameter_;
    const double rotaryTravel = da != 0 ? (std::fabs(da) / 360) * circumference : 0;

    struct AxisMove {
        double distance;
        double maxFeed;
        double acceleration;
    };
    std::vector<AxisMove> moves;
    moves.reserve(4);
    if (dx != 0) {
        moves.push_back({std::fabs(dx), options_.x.maxFeed, options_.x.acceleration});
    }
    if (dy != 0) {
        moves.push_back({std::fabs(dy), options_.y.maxFeed, options_.y.acceleration});
    }
    if (dz != 0) {
        moves.push_back({std::fabs(dz), options_.z.maxFeed, options_.z.acceleration});
    }
    if (da != 0) {
        // A rates are degrees; express them as surface speed on the stock.
        moves.push_back({rotaryTravel, options_.a.maxFeed / 360 * circumference,
                         options_.a.acceleration / 360 * circumference});
    }
    if (moves.empty()) {
        return;
    }

    const bool rapid = modal_.motion == "G0";
    const bool imperial = modal_.units == "G20";
    double duration = 0;

    if (da != 0 && (dx != 0 || dy != 0 || dz != 0)) {
        // Combined linear + rotary: every axis moves at once; the slowest wins.
        const double programmed = rapid ? 0 : (imperial ? feed_ * kInchToMm : feed_);
        for (const AxisMove& axis : moves) {
            const double axisFeed = std::min(rapid ? axis.maxFeed : programmed, axis.maxFeed);
            const double f = axisFeed / 60;
            if (f > 0) {
                duration = std::max(duration, acceleratedMoveTime(axis.distance, f, axis.acceleration));
            }
        }
    } else {
        const double travel = da != 0 ? rotaryTravel : linearTravel;
        double minMaxFeed = std::numeric_limits<double>::infinity();
        double minAcceleration = std::numeric_limits<double>::infinity();
        for (const AxisMove& axis : moves) {
            minMaxFeed = std::min(minMaxFeed, axis.maxFeed);
            minAcceleration = std::min(minAcceleration, axis.acceleration);
        }
        double feed = rapid ? minMaxFeed : feed_;
        if (da != 0 && dx == 0 && dy == 0 && dz == 0) {
            // Pure rotary: F is degrees/min; convert to surface speed.
            const double degreesPerMin = imperial ? feed * kInchToMm : feed;
            feed = degreesPerMin / 360 * circumference;
        } else if (!rapid && imperial) {
            feed *= kInchToMm;
        }
        feed = std::min(feed, minMaxFeed);
        const double f = feed / 60;
        if (f == lastF_ && da == 0) {
            duration = f != 0 ? travel / f : 0;
        } else {
            duration = acceleratedMoveTime(travel, f, minAcceleration);
        }
        lastF_ = f;
    }
    addTime(duration);
}

// Trapezoidal move time (from Slic3r), as used by gSender's estimator.
double Interpreter::acceleratedMoveTime(double length, double velocity, double acceleration) {
    if (!(velocity > 0)) {
        return 0;
    }
    const double accel = acceleration == 0 ? 750 : acceleration;
    double halfLength = length / 2;
    const double initTime = velocity / accel;
    const double initDistance = 0.5 * velocity * initTime;
    double time = 0;
    if (halfLength >= initDistance) {
        halfLength -= initDistance;
        time += initTime;
    }
    time += halfLength / velocity;
    return 2 * time;
}

Interpreter::Cmd Interpreter::gCommand(double code) noexcept {
    if (code == 0) return Cmd::G0;
    if (code == 1) return Cmd::G1;
    if (code == 2) return Cmd::G2;
    if (code == 3) return Cmd::G3;
    if (code == 4) return Cmd::G4;
    if (code == 10) return Cmd::G10;
    if (code == 17) return Cmd::G17;
    if (code == 18) return Cmd::G18;
    if (code == 19) return Cmd::G19;
    if (code == 20) return Cmd::G20;
    if (code == 21) return Cmd::G21;
    if (code == 38.2) return Cmd::G38_2;
    if (code == 38.3) return Cmd::G38_3;
    if (code == 38.4) return Cmd::G38_4;
    if (code == 38.5) return Cmd::G38_5;
    if (code == 43.1) return Cmd::G43_1;
    if (code == 49) return Cmd::G49;
    if (code == 54) return Cmd::G54;
    if (code == 55) return Cmd::G55;
    if (code == 56) return Cmd::G56;
    if (code == 57) return Cmd::G57;
    if (code == 58) return Cmd::G58;
    if (code == 59) return Cmd::G59;
    if (code == 80) return Cmd::G80;
    if (code == 90) return Cmd::G90;
    if (code == 91) return Cmd::G91;
    if (code == 92) return Cmd::G92;
    if (code == 92.1) return Cmd::G92_1;
    if (code == 93) return Cmd::G93;
    if (code == 94) return Cmd::G94;
    if (code == 95) return Cmd::G95;
    return Cmd::None;
}

Interpreter::Cmd Interpreter::mCommand(double code) noexcept {
    if (code == 0) return Cmd::M0;
    if (code == 1) return Cmd::M1;
    if (code == 2) return Cmd::M2;
    if (code == 30) return Cmd::M30;
    if (code == 3) return Cmd::M3;
    if (code == 4) return Cmd::M4;
    if (code == 5) return Cmd::M5;
    if (code == 6) return Cmd::M6;
    if (code == 7) return Cmd::M7;
    if (code == 8) return Cmd::M8;
    if (code == 9) return Cmd::M9;
    return Cmd::None;
}

std::string_view Interpreter::commandName(Cmd cmd) noexcept {
    switch (cmd) {
        case Cmd::G0: return "G0";
        case Cmd::G1: return "G1";
        case Cmd::G2: return "G2";
        case Cmd::G3: return "G3";
        case Cmd::G4: return "G4";
        case Cmd::G10: return "G10";
        case Cmd::G17: return "G17";
        case Cmd::G18: return "G18";
        case Cmd::G19: return "G19";
        case Cmd::G20: return "G20";
        case Cmd::G21: return "G21";
        case Cmd::G38_2: return "G38.2";
        case Cmd::G38_3: return "G38.3";
        case Cmd::G38_4: return "G38.4";
        case Cmd::G38_5: return "G38.5";
        case Cmd::G43_1: return "G43.1";
        case Cmd::G49: return "G49";
        case Cmd::G54: return "G54";
        case Cmd::G55: return "G55";
        case Cmd::G56: return "G56";
        case Cmd::G57: return "G57";
        case Cmd::G58: return "G58";
        case Cmd::G59: return "G59";
        case Cmd::G80: return "G80";
        case Cmd::G90: return "G90";
        case Cmd::G91: return "G91";
        case Cmd::G92: return "G92";
        case Cmd::G92_1: return "G92.1";
        case Cmd::G93: return "G93";
        case Cmd::G94: return "G94";
        case Cmd::G95: return "G95";
        case Cmd::M0: return "M0";
        case Cmd::M1: return "M1";
        case Cmd::M2: return "M2";
        case Cmd::M30: return "M30";
        case Cmd::M3: return "M3";
        case Cmd::M4: return "M4";
        case Cmd::M5: return "M5";
        case Cmd::M6: return "M6";
        case Cmd::M7: return "M7";
        case Cmd::M8: return "M8";
        case Cmd::M9: return "M9";
        case Cmd::None: return "";
    }
    return "";
}

bool Interpreter::isMotion(Cmd cmd) noexcept {
    switch (cmd) {
        case Cmd::G0:
        case Cmd::G1:
        case Cmd::G2:
        case Cmd::G3:
        case Cmd::G38_2:
        case Cmd::G38_3:
        case Cmd::G38_4:
        case Cmd::G38_5:
            return true;
        default:
            return false;
    }
}

}  // namespace gs::gcode
