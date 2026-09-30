#include "gs/util/wizard_walk.hpp"

#include <algorithm>

namespace gs::util {

void WizardWalk::start(int stepCount, bool hasCompletion, SkipStep skip) {
    stepCount_ = std::max(0, stepCount);
    hasCompletion_ = hasCompletion;
    skip_ = std::move(skip);
    done_.clear();
    enter(0);
}

void WizardWalk::clear() {
    stepCount_ = 0;
    hasCompletion_ = false;
    skip_ = {};
    step_ = 0;
    done_.clear();
    atCompletion_ = false;
}

// A step with nothing to do counts as done and is passed; the last one
// skipped leads to the closing page, when there is one.
void WizardWalk::enter(int step) {
    const int last = stepCount_ - 1;
    atCompletion_ = false;
    while (step <= last && skipped(step)) {
        done_.insert(step);
        if (step == last) {
            atCompletion_ = hasCompletion_;
            break;
        }
        ++step;
    }
    step_ = std::clamp(step, 0, std::max(0, last));
}

bool WizardWalk::setStepDone(bool done) {
    if (!active() || done == isDone(step_)) {
        return false;
    }
    if (done) {
        done_.insert(step_);
    } else {
        done_.erase(step_);
    }
    return true;
}

bool WizardWalk::canNext() const {
    return active() && !atCompletion_ && isDone(step_) && (step_ + 1 < stepCount_ || hasCompletion_);
}

bool WizardWalk::canBack() const {
    return active() && !atCompletion_ && step_ > 0;
}

bool WizardWalk::next() {
    if (!canNext()) {
        return false;
    }
    if (step_ + 1 < stepCount_) {
        enter(step_ + 1);
    } else {
        atCompletion_ = true;
    }
    return true;
}

bool WizardWalk::back() {
    if (!canBack()) {
        return false;
    }
    int step = step_ - 1;
    while (step > 0 && skipped(step)) {
        --step;
    }
    step_ = step;
    return true;
}

bool WizardWalk::restart() {
    if (!active()) {
        return false;
    }
    done_.clear();
    enter(0);
    return true;
}

}  // namespace gs::util
