#pragma once

// A wizard's steps walked one at a time (upstream's components/Wizard):
// Next opens once the step at hand is done; a step with nothing to do
// (autoComplete) counts as done and is passed; past the last step comes the
// closing page, when the wizard has one. Previous goes back over skipped
// steps. The steps themselves are the caller's: the walk knows them by index.

#include <functional>
#include <set>

namespace gs::util {

class WizardWalk {
public:
    using SkipStep = std::function<bool(int step)>;

    // A walk over `stepCount` steps from the first; `skip` says which have
    // nothing to do (none when empty).
    void start(int stepCount, bool hasCompletion, SkipStep skip = {});
    // No walk (the wizard closed).
    void clear();

    bool active() const noexcept { return stepCount_ > 0; }
    int step() const noexcept { return step_; }
    int stepCount() const noexcept { return stepCount_; }
    bool atCompletion() const noexcept { return atCompletion_; }
    bool isDone(int step) const { return done_.contains(step); }

    // The step at hand's page says whether it is done; false when that
    // changes nothing.
    bool setStepDone(bool done);
    bool canNext() const;
    bool canBack() const;
    // Each false when it may not move.
    bool next();  // the next step, or the closing page after the last
    bool back();  // the step before, passing those skipped
    bool restart();  // the first step again, nothing done

private:
    void enter(int step);
    bool skipped(int step) const { return skip_ && skip_(step); }

    int stepCount_ = 0;
    bool hasCompletion_ = false;
    SkipStep skip_;
    int step_ = 0;
    std::set<int> done_;
    bool atCompletion_ = false;
};

}  // namespace gs::util
