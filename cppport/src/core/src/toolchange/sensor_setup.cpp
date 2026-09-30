#include "gs/toolchange/sensor_setup.hpp"

namespace gs::toolchange {

void ContinuityCheck::restart(bool pinOn) noexcept {
    phase_ = Phase::Checking;
    update(pinOn);
}

bool ContinuityCheck::update(bool pinOn) noexcept {
    const Phase before = phase_;
    if (phase_ == Phase::Checking) {
        phase_ = pinOn ? Phase::StuckOn : Phase::Waiting;
    } else if (phase_ == Phase::Waiting && pinOn) {
        phase_ = Phase::Success;
    }
    return phase_ != before;
}

void PositionFollower::start(std::optional<Position> machine, bool keepUntilMoved) noexcept {
    atStart_ = machine;
    setAt_.reset();
    keepUntilMoved_ = keepUntilMoved;
    editing_ = false;
    set_ = false;
}

bool PositionFollower::edit() noexcept {
    editing_ = true;
    const bool wasSet = set_;
    set_ = false;
    return wasSet;
}

void PositionFollower::set(std::optional<Position> machine) noexcept {
    setAt_ = machine;
    set_ = true;
}

PositionFollower::Follow PositionFollower::machineAt(std::optional<Position> machine) noexcept {
    Follow follow;
    if (editing_ || !machine) {
        return follow;
    }
    if (keepUntilMoved_ && (!atStart_ || *atStart_ == *machine)) {
        return follow;  // no real jog since the step opened: keep the recommendation
    }
    if (set_ && setAt_ && *setAt_ != *machine) {
        set_ = false;
        follow.unset = true;
    }
    follow.show = machine;
    return follow;
}

}  // namespace gs::toolchange
