// Afterglow - Combined control presses, group volume and the relay stall guard.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include "afterglow/controls.hpp"
#include <gtest/gtest.h>

using namespace hui::afterglow;

namespace
{
constexpr std::int64_t kSecond = 1000000;

TEST(AfterglowControls, HeldVolumeAndQuickSeeksAddUpBehindTheCommandInFlight)
{
    ControlQueue queue;
    ASSERT_TRUE(queue.press(Control::volume, 5));
    const auto first = queue.take(0);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->amount, 5);
    EXPECT_FALSE(queue.take(0).has_value()) << "one command in flight at a time";
    for (int i = 0; i < 3; ++i)
        queue.press(Control::volume, 5);
    queue.press(Control::volume, -5);
    queue.press(Control::seek, 10);
    queue.press(Control::seek, 10);
    queue.press(Control::seek, -10);
    ASSERT_EQ(queue.waiting().size(), 2u) << "one waiting job per kind";
    EXPECT_EQ(queue.waiting()[0].amount, 10) << "no press is dropped: +15 then -5";
    EXPECT_EQ(queue.waiting()[1].amount, 10);
    queue.settle();
    const auto second = queue.take(kSecond);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->kind, Control::volume);
    EXPECT_EQ(second->amount, 10);
}

TEST(AfterglowControls, SkipsCollapseToOneWaitingSkipPerDirection)
{
    ControlQueue queue;
    queue.press(Control::next);
    ASSERT_TRUE(queue.take(0).has_value()); // under way
    for (int i = 0; i < 6; ++i)
        EXPECT_TRUE(queue.press(Control::next));
    queue.press(Control::previous);
    queue.press(Control::previous);
    ASSERT_EQ(queue.waiting().size(), 2u);
    EXPECT_EQ(queue.waiting()[0].kind, Control::next);
    EXPECT_EQ(queue.waiting()[1].kind, Control::previous);
}

TEST(AfterglowControls, TogglesQueueInOrderAndTheQueueIsBounded)
{
    ControlQueue queue;
    for (std::size_t i = 0; i < ControlQueue::kMaxWaiting; ++i)
        EXPECT_TRUE(queue.press(Control::toggle));
    EXPECT_FALSE(queue.press(Control::toggle)) << "a full queue refuses";
    queue.clear();
    EXPECT_TRUE(queue.waiting().empty());
    EXPECT_FALSE(queue.in_flight(0));
}

TEST(AfterglowControls, AnUnansweredCommandStopsHoldingTheQueue)
{
    ControlQueue queue;
    queue.press(Control::next);
    queue.press(Control::previous);
    ASSERT_TRUE(queue.take(0).has_value());
    EXPECT_FALSE(queue.take(ControlQueue::kReplyUs - 1).has_value());
    const auto after = queue.take(ControlQueue::kReplyUs);
    ASSERT_TRUE(after.has_value()) << "no reply in 10 s: the next press goes out";
    EXPECT_EQ(after->kind, Control::previous);
}

TEST(AfterglowControls, AssumedValueOutranksTheReportUntilItCatchesUpOrAges)
{
    Assumed volume;
    EXPECT_EQ(volume.value_or("a", 40, 0), 40);
    volume.sent("a", 45, 0);
    EXPECT_EQ(volume.value_or("a", 40, kSecond), 45) << "the report has not caught up";
    EXPECT_EQ(volume.value_or("b", 20, kSecond), 20) << "another speaker's level is its own";
    volume.sent("a", 50, 0);
    EXPECT_EQ(volume.value_or("a", 50, kSecond), 50);
    EXPECT_FALSE(volume.fresh("a", kSecond)) << "forgotten once reported";
    volume.sent("a", 55, 0);
    EXPECT_EQ(volume.value_or("a", 40, Assumed::kFreshUs + 1), 40) << "too old to trust";
    volume.sent("q:item", 43, 0);
    EXPECT_TRUE(volume.fresh("q:item", Assumed::kFreshUs));
    EXPECT_FALSE(volume.fresh("q:other", 0)) << "a new song starts from its report";
}

TEST(AfterglowControls, GroupsAndSyncLeadersUseTheGroupVolumeCommand)
{
    const auto group = companion_wire::volume_target(
        R"({"player_id":"syncgroup_x","type":"group","volume_level":null,"group_volume":100,)"
        R"("group_members":["a","b"]})");
    EXPECT_TRUE(group.group);
    EXPECT_EQ(group.level, 100) << "a group's level is group_volume";
    const auto leader = companion_wire::volume_target(
        R"({"player_id":"lead","type":"player","volume_level":70,"group_volume":40,)"
        R"("group_members":["lead","member"]})");
    EXPECT_TRUE(leader.group);
    EXPECT_EQ(leader.level, 40);
    const auto alone = companion_wire::volume_target(
        R"({"player_id":"solo","type":"player","volume_level":47,"group_volume":47,)"
        R"("group_members":["solo"]})");
    EXPECT_FALSE(alone.group) << "a leader listing only itself is no group";
    EXPECT_EQ(alone.level, 47);
    auto request = companion_wire::volume_request(leader, 55);
    EXPECT_EQ(request.command, "players/cmd/group_volume");
    EXPECT_EQ(request.args, R"({"player_id":"lead","volume_level":55})");
    request = companion_wire::volume_request(alone, 140);
    EXPECT_EQ(request.command, "players/cmd/volume_set");
    EXPECT_EQ(request.args, R"({"player_id":"solo","volume_level":100})");
    EXPECT_FALSE(companion_wire::volume_request({}, 10).error.empty());
}

TEST(AfterglowControls, OneSpeakersVolumeStepsAddUpPerSpeaker)
{
    ControlQueue queue;
    ASSERT_TRUE(queue.press(Control::member_volume, 5, "kitchen"));
    ASSERT_TRUE(queue.take(0).has_value());
    queue.press(Control::member_volume, 5, "kitchen");
    queue.press(Control::member_volume, 5, "den");
    queue.press(Control::member_volume, 5, "kitchen");
    queue.press(Control::volume, -5);
    ASSERT_EQ(queue.waiting().size(), 3u) << "one waiting job per speaker, apart from the group's";
    EXPECT_EQ(queue.waiting()[0].player, "kitchen");
    EXPECT_EQ(queue.waiting()[0].amount, 10);
    EXPECT_EQ(queue.waiting()[1].player, "den");
    EXPECT_EQ(queue.waiting()[1].amount, 5);
    EXPECT_TRUE(queue.waiting()[2].player.empty());
}

TEST(AfterglowControls, AVolumeCanBeSetUnlessFixedOrAGroupPlayer)
{
    using companion_wire::volume_settable;
    EXPECT_TRUE(volume_settable(
        R"({"type":"player","volume_control":"native","volume_level":20,"supported_features":["volume_set","set_members"]})"));
    EXPECT_TRUE(volume_settable(
        R"({"type":"player","volume_control":"uuid:linked","volume_level":20,"supported_features":["volume_set"]})"))
        << "a linked control sets it";
    EXPECT_FALSE(volume_settable(
        R"({"type":"player","volume_control":"none","volume_level":null,"supported_features":[]})"))
        << "fixed volume";
    EXPECT_FALSE(volume_settable(
        R"({"type":"player","volume_control":"native","volume_level":null,"supported_features":["volume_set"]})"))
        << "no level to show";
    EXPECT_FALSE(volume_settable(
        R"({"type":"group","volume_control":"native","volume_level":20,"supported_features":["volume_set"]})"))
        << "a group player has the group volume";
    EXPECT_FALSE(volume_settable(
        R"({"type":"player","volume_level":20,"supported_features":["play_media"]})"));
}

TEST(AfterglowControls, RelayReconnectsWhenFramesStopMidStreamAndSkipsASilentSocket)
{
    using companion_wire::RelayCheck;
    const auto stall = companion_wire::kRelayStallUs;
    // Streaming: frames stopped without stream/end for 10 s -> a fresh socket.
    EXPECT_EQ(companion_wire::relay_check(5 * kSecond, 0, 5 * kSecond - 1, true), RelayCheck::keep);
    EXPECT_EQ(companion_wire::relay_check(5 * kSecond + stall, 0, 5 * kSecond, true),
              RelayCheck::reconnect);
    // A fresh socket that never streams is skipped, not reconnected again: no loop.
    EXPECT_EQ(companion_wire::relay_check(stall, 0, 0, false), RelayCheck::keep);
    EXPECT_EQ(companion_wire::relay_check(stall + 1, 0, 0, false), RelayCheck::skip);
    // After a stream/end the socket waits for the same player to resume.
    EXPECT_EQ(companion_wire::relay_check(9 * kSecond, 0, 2 * kSecond, false), RelayCheck::keep);
    EXPECT_EQ(companion_wire::relay_check(13 * kSecond, 0, 2 * kSecond, false), RelayCheck::skip);
}
} // namespace

TEST(AfterglowControls, PressesForDifferentTargetsStayApart)
{
    ControlQueue queue;
    queue.press(Control::volume, 5, "bedroom", "bedroom");
    queue.press(Control::volume, 5, "living", "living");
    queue.press(Control::volume, -5, "bedroom", "bedroom");
    const auto first = queue.take(0);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->queue, "bedroom");
    EXPECT_EQ(first->player, "bedroom");
    EXPECT_EQ(first->amount, 0) << "the bedroom steps collapse together";
}
