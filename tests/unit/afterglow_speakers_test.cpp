// Afterglow - The command target, moving music between speakers and Speakers together.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/speakers.hpp"
#include <gtest/gtest.h>

namespace
{
using namespace hui::afterglow;

SpeakerInfo speaker(const char *id, const char *name, const char *type = "player")
{
    SpeakerInfo p;
    p.id = id;
    p.name = name;
    p.type = type;
    p.available = true;
    return p;
}
QueueInfo queue(const char *id, const char *state, bool has_item = true)
{
    QueueInfo q;
    q.id = id;
    q.state = state;
    q.has_item = has_item;
    return q;
}

// Music Assistant as LG's pick tests describe it: a living room playing, a
// kitchen synced to it, an idle bedroom, a sync group and this PS5.
class AfterglowSpeakers : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        add(speaker("living", "Living room"));
        add(speaker("kitchen", "Kitchen"));
        players_["kitchen"].synced_to = "living";
        add(speaker("bedroom", "Bedroom"));
        add(speaker("group", "All rooms", "group"));
        add(speaker("ps5", "Afterglow PS5"));
        for (const char *id : {"living", "bedroom", "group", "ps5"})
            queues_[id] = queue(id, "idle", false);
        queues_["living"] = queue("living", "playing");
    }
    void add(SpeakerInfo p)
    {
        players_[p.id] = std::move(p);
    }
    QueueLookup lookup() const
    {
        return [this](const std::string &id) -> const QueueInfo *
        {
            const auto it = queues_.find(id);
            return it == queues_.end() ? nullptr : &it->second;
        };
    }
    TargetPick target(const std::string &chosen, const std::string &following = {},
                      const std::string &last = {}, const std::string &own = {}) const
    {
        return command_target(players_, lookup(), chosen, following, last, own);
    }
    Speakers players_;
    std::map<std::string, QueueInfo> queues_;
};

TEST_F(AfterglowSpeakers, AudibleQueueFollowsSyncThenGroupThenSource)
{
    EXPECT_EQ(audible_queue("kitchen", players_, lookup()), "living");
    players_["bedroom"].active_group = "group";
    EXPECT_EQ(audible_queue("bedroom", players_, lookup()), "group");
    players_["bedroom"].active_group.clear();
    players_["bedroom"].active_source = "elsewhere";
    EXPECT_EQ(audible_queue("bedroom", players_, lookup()), "")
        << "a source Music Assistant does not decode has no queue";
    players_["living"].synced_to = "kitchen";
    EXPECT_EQ(audible_queue("kitchen", players_, lookup()), "") << "a loop ends, it never spins";
    players_["living"].synced_to.clear();
    players_["living"].available = false;
    EXPECT_EQ(audible_queue("kitchen", players_, lookup()), "");
    EXPECT_EQ(audible_queue("nobody", players_, lookup()), "");
}

TEST_F(AfterglowSpeakers, TheChosenSpeakerTakesCommandsEvenWhileSilent)
{
    const auto bedroom = target("bedroom", "living");
    EXPECT_EQ(bedroom.queue_id, "bedroom") << "its own idle queue answers Play";
    EXPECT_EQ(bedroom.player_id, "bedroom");
    EXPECT_EQ(bedroom.reason, TargetReason::chosen);
    const auto kitchen = target("kitchen");
    EXPECT_EQ(kitchen.queue_id, "living") << "a synced speaker sounds its leader's queue";
    EXPECT_EQ(kitchen.player_id, "kitchen");
    players_["bedroom"].enabled = false;
    EXPECT_EQ(target("bedroom", "living").reason, TargetReason::following)
        << "a disabled choice falls through to what is followed";
}

TEST_F(AfterglowSpeakers, WithoutAChoiceTheFollowedThenLastThenThisPs5ThenTheFirstSpeaker)
{
    const auto following = target("", "living", "bedroom", "ps5");
    EXPECT_EQ(following.queue_id, "living");
    EXPECT_EQ(following.reason, TargetReason::following);
    const auto last = target("", "", "bedroom", "ps5");
    EXPECT_EQ(last.queue_id, "bedroom");
    EXPECT_EQ(last.reason, TargetReason::last);
    const auto own = target("", "", "", "ps5");
    EXPECT_EQ(own.queue_id, "ps5");
    EXPECT_EQ(own.reason, TargetReason::this_ps5);
    const auto first = target("", "gone", "gone");
    EXPECT_EQ(first.queue_id, "ps5") << "Afterglow PS5 sorts first by name";
    EXPECT_EQ(first.reason, TargetReason::first);
    players_["ps5"].hidden = true;
    players_["bedroom"].hidden = true;
    EXPECT_EQ(target("").player_id, "kitchen")
        << "real speakers before groups, by name (Kitchen sounds the living room's queue)";
    EXPECT_EQ(target("").queue_id, "living");
    queues_.clear();
    EXPECT_TRUE(target("bedroom", "living").queue_id.empty()) << "no queue anywhere: no target";
    EXPECT_EQ(target("").reason, TargetReason::none);
}

TEST_F(AfterglowSpeakers, OnlyMusicThatCanMoveMovesAndOnlyToASilentSpeaker)
{
    EXPECT_TRUE(moves_music(players_, lookup(), "living", "bedroom", false));
    EXPECT_FALSE(moves_music(players_, lookup(), "living", "kitchen", false))
        << "the kitchen already sounds it";
    EXPECT_FALSE(moves_music(players_, lookup(), "living", "living", false));
    queues_["bedroom"].state = "playing";
    EXPECT_FALSE(moves_music(players_, lookup(), "living", "bedroom", false))
        << "a speaker playing its own music is followed, not overwritten";
    queues_["bedroom"].state = "paused";
    EXPECT_TRUE(moves_music(players_, lookup(), "living", "bedroom", false));
    queues_["living"].state = "idle";
    EXPECT_FALSE(moves_music(players_, lookup(), "living", "bedroom", false))
        << "stopped music stays unless asked";
    EXPECT_TRUE(moves_music(players_, lookup(), "living", "bedroom", true));
    queues_["living"].has_item = false;
    EXPECT_FALSE(moves_music(players_, lookup(), "living", "bedroom", true)) << "nothing to move";
    queues_["living"] = queue("living", "paused");
    players_["bedroom"].available = false;
    EXPECT_FALSE(moves_music(players_, lookup(), "living", "bedroom", false));
    EXPECT_FALSE(moves_music(players_, lookup(), "", "bedroom", false));
    EXPECT_FALSE(moves_music(players_, lookup(), "living", "nobody", false));
}

TEST_F(AfterglowSpeakers, GroupsAndLeadersWithMembersTakeTheGroupVolume)
{
    EXPECT_TRUE(group_player(players_["group"]));
    EXPECT_FALSE(group_player(players_["living"]));
    players_["living"].group_members = {"living"};
    EXPECT_FALSE(group_player(players_["living"])) << "only itself is no group";
    players_["living"].group_members = {"living", "kitchen"};
    EXPECT_TRUE(group_player(players_["living"]));
}

TEST(AfterglowSpeakersTogether, ListsRealSpeakersWithTicksAndExplainsWhoCannotJoin)
{
    CompanionPlayer lead("lead", "Lead");
    lead.can_set_members = true;
    lead.group_members = {"lead", "fixed", "friend"};
    lead.static_members = {"fixed"};
    lead.can_group_with = {"friend", "sendspin"};
    CompanionPlayer fixed("fixed", "Fixed");
    CompanionPlayer by_provider("cousin", "Cousin");
    by_provider.provider = "sendspin";
    CompanionPlayer stranger("stranger", "Stranger");
    stranger.provider = "airplay";
    CompanionPlayer member("friend", "Friend");
    CompanionPlayer group("all", "All rooms");
    group.type = "group";
    const std::vector<CompanionPlayer> players{group, lead, fixed, by_provider, stranger, member};
    const auto options = group_options(players, "lead");
    ASSERT_EQ(options.size(), 5U) << "groups are not offered as members";
    EXPECT_TRUE(options[0].leader);
    EXPECT_TRUE(options[0].member);
    EXPECT_FALSE(options[0].allowed);
    EXPECT_TRUE(options[1].member) << "Fixed";
    EXPECT_FALSE(options[1].allowed) << "a fixed member stays";
    EXPECT_FALSE(options[2].member) << "Cousin";
    EXPECT_TRUE(options[2].allowed) << "can_group_with names its provider";
    EXPECT_FALSE(options[3].allowed) << "Stranger";
    EXPECT_TRUE(options[4].member) << "Friend";
    EXPECT_TRUE(options[4].allowed) << "a member can leave";

    auto unable = players;
    unable[1].can_set_members = false;
    for (const auto &option : group_options(unable, "lead"))
        EXPECT_FALSE(option.allowed) << option.name;
    EXPECT_TRUE(group_options(players, "").empty());
    EXPECT_TRUE(group_options(players, "nobody").empty());
}
} // namespace
