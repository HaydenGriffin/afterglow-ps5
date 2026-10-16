// Afterglow - Joining the music elsewhere, what each speaker plays, one speaker's volume.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/speakers.hpp"
#include <string>
#include <vector>

// The same rules as LG Afterglow's speaker-group.js, over the lists the
// companion keeps (speakers.hpp). Pure: the companion and the panel call them.
namespace hui::afterglow
{
// What a speaker plays, from its audible queue: "Song — Artist",
// "Paused · Song — Artist", or "Idle" when nothing sounds.
std::string speaker_activity(const std::string &player_id, const Speakers &players,
                             const QueueLookup &queues);

// Whether this PS5's own player can join the leader's group: the leader's
// queue holds a song that plays or is paused, the own player sounds neither
// it nor music of its own, and the grouping rules (group_options) let it in.
// Empty leader when it cannot.
struct JoinPlan
{
    std::string leader, member;
};
JoinPlan join_plan(const std::vector<CompanionPlayer> &list, const Speakers &players,
                   const QueueLookup &queues, const std::string &leader, const std::string &own);

// The name a leader goes by: its player's, else its id.
std::string leader_name(const Speakers &players, const std::string &leader);

// Fills each listed speaker's activity, volume and whether it can be set.
void describe_speakers(std::vector<CompanionPlayer> &list, const Speakers &players,
                       const QueueLookup &queues);

// Volume for each speaker: the leader's group members (the leader too, in
// group_options order) whose volume can be set; none while it is one speaker.
struct MemberVolume
{
    std::string id, name;
    int level = 0;
};
std::vector<MemberVolume> member_volumes(const std::vector<CompanionPlayer> &list,
                                         const std::string &leader);
} // namespace hui::afterglow
