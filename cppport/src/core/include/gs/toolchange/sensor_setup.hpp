#pragma once

// The Sienci TLS installer's two interactive pages
// (features/AccessoryInstaller): ContinuityIndicator, which proves the
// sensor's wiring, and PositionSetter, whose X/Y/Z fields follow the
// machine until they are edited or set.

#include <array>
#include <optional>

namespace gs::toolchange {

// Press the TLS to prove its wiring: a pin already on when the check
// starts is a short (StuckOn); a press while waiting is Success.
class ContinuityCheck {
public:
    enum class Phase { Checking, Waiting, Success, StuckOn };

    Phase phase() const noexcept { return phase_; }
    // From the start again, with the pin as it is now.
    void restart(bool pinOn) noexcept;
    // The pin as it is now; true when the phase changed.
    bool update(bool pinOn) noexcept;

private:
    Phase phase_ = Phase::Checking;
};

// PositionSetter's fields: they show the machine's position as it moves
// until someone edits them. A position that was set is undone when the
// machine moves off it. The manual tool change location starts at a
// recommended position and keeps it until the machine first moves.
class PositionFollower {
public:
    using Position = std::array<double, 3>;

    // The step opened with the machine at `machine` (none without a
    // controller); `keepUntilMoved` for the manual location.
    void start(std::optional<Position> machine, bool keepUntilMoved) noexcept;
    // Typed into: the fields stop following. True when that undid a set
    // position.
    bool edit() noexcept;
    // Set Position with the machine at `machine`.
    void set(std::optional<Position> machine) noexcept;
    bool isSet() const noexcept { return set_; }

    struct Follow {
        std::optional<Position> show;  // what the fields show now, if they follow
        bool unset = false;            // a set position was undone
    };
    Follow machineAt(std::optional<Position> machine) noexcept;

private:
    std::optional<Position> atStart_;
    std::optional<Position> setAt_;
    bool keepUntilMoved_ = false;
    bool editing_ = false;
    bool set_ = false;
};

}  // namespace gs::toolchange
