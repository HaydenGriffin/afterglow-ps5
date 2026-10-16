// Afterglow - Joining the music elsewhere, what each speaker plays, one speaker's volume.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/speaker_group.hpp"
#include <gtest/gtest.h>

namespace
{
using namespace hui::afterglow;

// LG speaker-group.test.mjs's house: a living room playing a song with two
// artists, this PS5 idle, a paused den that cannot lead this provider.
class AfterglowSpeakerGroup : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        add("lead", "Living room");
        add("ps5", "This PS5");
        add("den", "Den");
        queue("lead", "playing", "Low tide", "Demo One, Demo Two");
        queue("ps5", "idle");
        queue("den", "paused", "Second kettle");
        list_ = {player("lead", "Living room"), player("ps5", "This PS5"), player("den", "Den")};
        list_[0].can_set_members = true;
        list_[0].can_group_with = {"sendspin"};
    }
    void add(const char *id, const char *name)
    {
        SpeakerInfo p;
        p.id = id;
        p.name = name;
        p.type = "player";
        p.available = true;
        p.volume = 30;
        p.volume_settable = true;
        players_[id] = std::move(p);
    }
    void queue(const char *id, const char *state, const char *title = "", const char *artist = "")
    {
        QueueInfo q;
        q.id = id;
        q.state = state;
        q.has_item = *title != '\0';
        q.title = title;
        q.artist = artist;
        queues_[id] = std::move(q);
    }
    static CompanionPlayer player(const char *id, const char *name)
    {
        CompanionPlayer p{id, name};
        p.type = "player";
        p.provider = "sendspin";
        return p;
    }
    QueueLookup lookup() const
    {
        return [this](const std::string &id) -> const QueueInfo *
        {
            const auto it = queues_.find(id);
            return it == queues_.end() ? nullptr : &it->second;
        };
    }
    JoinPlan plan(const std::string &leader, const std::string &own = "ps5") const
    {
        return join_plan(list_, players_, lookup(), leader, own);
    }
    Speakers players_;
    std::map<std::string, QueueInfo> queues_;
    std::vector<CompanionPlayer> list_;
};

TEST_F(AfterglowSpeakerGroup, ThisPs5JoinsASpeakerThatPlaysWhenTheRulesLetItIn)
{
    EXPECT_EQ(plan("lead").leader, "lead");
    EXPECT_EQ(plan("lead").member, "ps5");
    EXPECT_TRUE(plan("lead", "").leader.empty()) << "no own player, no join";
    EXPECT_TRUE(plan("ps5").leader.empty()) << "never itself";
    EXPECT_TRUE(plan("den").leader.empty()) << "the den cannot lead a group";
    queues_["lead"].state = "paused";
    EXPECT_EQ(plan("lead").leader, "lead") << "paused music can be joined";
    queues_["lead"].state = "idle";
    EXPECT_TRUE(plan("lead").leader.empty()) << "stopped music is not joined";
    queues_["lead"].state = "playing";
    queue("ps5", "playing", "Mine");
    EXPECT_TRUE(plan("lead").leader.empty()) << "this PS5 plays music of its own";
    queue("ps5", "idle");
    players_["ps5"].synced_to = "lead";
    EXPECT_TRUE(plan("lead").leader.empty()) << "already sounding it";
    players_["ps5"].synced_to.clear();
    players_["ps5"].available = false;
    EXPECT_TRUE(plan("lead").leader.empty()) << "unavailable";
    players_["ps5"].available = true;
    list_[0].can_set_members = false;
    EXPECT_TRUE(plan("lead").leader.empty()) << "a leader that cannot group";
    list_[0].can_set_members = true;
    list_[0].static_members = {"ps5"};
    EXPECT_TRUE(plan("lead").leader.empty()) << "a fixed member is not a choice";
    list_[0].static_members.clear();
    list_[0].group_members = {"lead", "ps5"};
    EXPECT_TRUE(plan("lead").leader.empty()) << "already a member";
    list_[0].group_members.clear();
    list_[0].can_group_with = {"ps5"};
    EXPECT_EQ(plan("lead").leader, "lead") << "can_group_with may name the player itself";
    EXPECT_EQ(leader_name(players_, "lead"), "Living room");
    EXPECT_EQ(leader_name(players_, "gone"), "gone");
}

TEST_F(AfterglowSpeakerGroup, EachSpeakerSaysWhatItPlays)
{
    EXPECT_EQ(speaker_activity("lead", players_, lookup()), "Low tide — Demo One, Demo Two");
    EXPECT_EQ(speaker_activity("den", players_, lookup()), "Paused · Second kettle");
    EXPECT_EQ(speaker_activity("ps5", players_, lookup()), "Idle");
    EXPECT_EQ(speaker_activity("missing", players_, lookup()), "Idle");
    players_["ps5"].synced_to = "lead";
    EXPECT_EQ(speaker_activity("ps5", players_, lookup()), "Low tide — Demo One, Demo Two")
        << "a member sounds its leader's song";
    queues_["den"].state = "idle";
    EXPECT_EQ(speaker_activity("den", players_, lookup()), "Idle")
        << "a stopped queue that keeps its song is idle";
    describe_speakers(list_, players_, lookup());
    EXPECT_EQ(list_[1].activity, "Low tide — Demo One, Demo Two");
    EXPECT_EQ(list_[1].volume, 30);
    EXPECT_TRUE(list_[1].volume_settable);
}

TEST_F(AfterglowSpeakerGroup, OnlyMembersWhoseVolumeCanBeSetGetARow)
{
    players_["den"].volume_settable = false;
    describe_speakers(list_, players_, lookup());
    EXPECT_TRUE(member_volumes(list_, "lead").empty()) << "one speaker has the Queue's volume";
    list_[0].group_members = {"lead", "ps5", "den"};
    players_["ps5"].volume = 55;
    describe_speakers(list_, players_, lookup());
    const auto volumes = member_volumes(list_, "lead");
    ASSERT_EQ(volumes.size(), 2u) << "the den's volume is fixed";
    EXPECT_EQ(volumes[0].name, "Living room");
    EXPECT_EQ(volumes[0].level, 30);
    EXPECT_EQ(volumes[1].id, "ps5");
    EXPECT_EQ(volumes[1].level, 55);
    EXPECT_TRUE(member_volumes(list_, "missing").empty());
}
} // namespace
