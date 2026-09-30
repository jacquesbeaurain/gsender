// The accessory wizards' walk through their steps (components/Wizard).

#include "gs/util/wizard_walk.hpp"

#include <gtest/gtest.h>

using gs::util::WizardWalk;

TEST(WizardWalk, NextOpensOnceTheStepIsDone) {
    WizardWalk walk;
    EXPECT_FALSE(walk.active());
    walk.start(3, true);
    EXPECT_EQ(walk.step(), 0);
    EXPECT_FALSE(walk.canNext());
    EXPECT_FALSE(walk.canBack());
    EXPECT_FALSE(walk.next());
    EXPECT_TRUE(walk.setStepDone(true));
    EXPECT_FALSE(walk.setStepDone(true));  // no change
    EXPECT_TRUE(walk.next());
    EXPECT_EQ(walk.step(), 1);
    EXPECT_TRUE(walk.canBack());
    EXPECT_TRUE(walk.back());
    EXPECT_EQ(walk.step(), 0);
    EXPECT_TRUE(walk.canNext());  // still done
    // An undone page closes Next again.
    EXPECT_TRUE(walk.setStepDone(false));
    EXPECT_FALSE(walk.canNext());
}

TEST(WizardWalk, TheLastStepLeadsToTheClosingPageWhenThereIsOne) {
    WizardWalk walk;
    walk.start(1, true);
    walk.setStepDone(true);
    EXPECT_TRUE(walk.next());
    EXPECT_TRUE(walk.atCompletion());
    EXPECT_FALSE(walk.canNext());
    EXPECT_FALSE(walk.canBack());
    EXPECT_TRUE(walk.restart());
    EXPECT_FALSE(walk.atCompletion());
    EXPECT_EQ(walk.step(), 0);
    EXPECT_FALSE(walk.isDone(0));

    walk.start(1, false);
    walk.setStepDone(true);
    EXPECT_FALSE(walk.canNext());  // nowhere to go
}

TEST(WizardWalk, StepsWithNothingToDoArePassed) {
    WizardWalk walk;
    const auto skipTwo = [](int step) { return step == 2; };
    walk.start(4, true, skipTwo);
    walk.setStepDone(true);
    walk.next();
    walk.setStepDone(true);
    walk.next();  // 2 is skipped
    EXPECT_EQ(walk.step(), 3);
    EXPECT_TRUE(walk.isDone(2));
    walk.back();  // and skipped going back
    EXPECT_EQ(walk.step(), 1);

    // Skipped to the end: the closing page, or the last step without one.
    walk.start(2, true, [](int step) { return step == 1; });
    walk.setStepDone(true);
    walk.next();
    EXPECT_TRUE(walk.atCompletion());
    walk.start(2, false, [](int step) { return step == 1; });
    walk.setStepDone(true);
    EXPECT_TRUE(walk.next());
    EXPECT_EQ(walk.step(), 1);
    EXPECT_FALSE(walk.atCompletion());
    EXPECT_FALSE(walk.canNext());

    // The first step is never skipped over going back.
    walk.start(3, true, [](int step) { return step == 0; });
    EXPECT_EQ(walk.step(), 1);
    EXPECT_TRUE(walk.back());
    EXPECT_EQ(walk.step(), 0);

    walk.clear();
    EXPECT_FALSE(walk.active());
    EXPECT_FALSE(walk.next());
}
