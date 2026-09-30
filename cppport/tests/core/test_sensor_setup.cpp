// The TLS installer's continuity check and position setter.

#include "gs/toolchange/sensor_setup.hpp"

#include <gtest/gtest.h>

using namespace gs::toolchange;
using Phase = ContinuityCheck::Phase;

TEST(ContinuityCheck, APressWhileWaitingProvesTheWiring) {
    ContinuityCheck check;
    check.restart(false);
    EXPECT_EQ(check.phase(), Phase::Waiting);
    EXPECT_FALSE(check.update(false));
    EXPECT_TRUE(check.update(true));
    EXPECT_EQ(check.phase(), Phase::Success);
    EXPECT_FALSE(check.update(false));  // released: still proven
    EXPECT_EQ(check.phase(), Phase::Success);
}

TEST(ContinuityCheck, APinOnAtTheStartIsAShort) {
    ContinuityCheck check;
    check.restart(true);
    EXPECT_EQ(check.phase(), Phase::StuckOn);
    EXPECT_FALSE(check.update(false));  // only Check Again starts over
    check.restart(false);
    EXPECT_EQ(check.phase(), Phase::Waiting);
}

TEST(PositionFollower, FieldsFollowTheMachineUntilEdited) {
    PositionFollower follower;
    follower.start(PositionFollower::Position{0, 0, 0}, false);
    EXPECT_EQ(follower.machineAt(PositionFollower::Position{1, 2, 3}).show, (PositionFollower::Position{1, 2, 3}));
    EXPECT_FALSE(follower.machineAt(std::nullopt).show);
    EXPECT_FALSE(follower.edit());
    EXPECT_FALSE(follower.machineAt(PositionFollower::Position{4, 5, 6}).show);
}

TEST(PositionFollower, ASetPositionIsUndoneWhenTheMachineMovesOff) {
    PositionFollower follower;
    follower.start(PositionFollower::Position{0, 0, 0}, false);
    follower.set(PositionFollower::Position{1, 1, 1});
    EXPECT_TRUE(follower.isSet());
    EXPECT_FALSE(follower.machineAt(PositionFollower::Position{1, 1, 1}).unset);
    const PositionFollower::Follow moved = follower.machineAt(PositionFollower::Position{2, 1, 1});
    EXPECT_TRUE(moved.unset);
    EXPECT_FALSE(follower.isSet());
    follower.set(PositionFollower::Position{2, 1, 1});
    EXPECT_TRUE(follower.edit());  // typing undoes it too
}

TEST(PositionFollower, TheManualLocationKeepsItsRecommendationUntilAJog) {
    PositionFollower follower;
    follower.start(PositionFollower::Position{0, 0, 0}, true);
    EXPECT_FALSE(follower.machineAt(PositionFollower::Position{0, 0, 0}).show);
    EXPECT_TRUE(follower.machineAt(PositionFollower::Position{0, 5, 0}).show);
    PositionFollower noController;
    noController.start(std::nullopt, true);
    EXPECT_FALSE(noController.machineAt(PositionFollower::Position{0, 5, 0}).show);
}
