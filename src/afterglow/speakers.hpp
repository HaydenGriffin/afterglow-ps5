// Afterglow - Which speaker commands act on, moving music between speakers, grouping.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/companion.hpp"
#include <functional>
#include <map>
#include <string>
#include <vector>

// Pure rules shared with LG Afterglow (pick.js, playback.js). The companion
// keeps the player and queue lists; these only read them.
//
// What sounds on a speaker is a queue, and in a sync group every member
// sounds the leader's queue, so a speaker is first resolved to its audible
// queue the way Music Assistant does: synced_to, then active_group, then
// active_source.
namespace hui::afterglow
{
// What the rules read from players/all.
struct SpeakerInfo
{
    std::string id, name, type, synced_to, active_group, active_source;
    bool available = false, enabled = true, hidden = false;
    int volume = 100, group_volume = -1;
    std::vector<std::string> group_members;
    // Music Assistant can set its volume: volume_set, not fixed ("none"), a level to show.
    bool volume_settable = false;
};
// What the rules read from player_queues/all: a queue's state and whether it
// holds a song (current_item).
struct QueueInfo
{
    std::string id, state;
    bool has_item = false;
    // The song it holds, for what each speaker plays (speaker_group.hpp).
    std::string title, artist;
};
using Speakers = std::map<std::string, SpeakerInfo>;
// nullptr when Music Assistant has no such queue.
using QueueLookup = std::function<const QueueInfo *(const std::string &queue_id)>;

// The queue sounding on a player, or "" when there is none Music Assistant
// decodes itself (a speaker playing its own input).
std::string audible_queue(const std::string &player_id, const Speakers &players,
                          const QueueLookup &queues);

// The queue a command acts on, which is not always the one being drawn: a
// speaker picked in Playing on takes commands before it plays, and from a
// cold start something must still answer Play. In order:
//   chosen     the picked speaker's audible queue, or its own when idle
//   following  the queue drawn now (kept after its music stops)
//   last       the queue followed before a reconnect
//   this_ps5   this PS5's own player, while the PS5 is the one sounding
//   first      the first speaker by name, real speakers before groups
// An empty queue_id when Music Assistant has no usable speaker at all.
struct TargetPick
{
    std::string queue_id, player_id;
    TargetReason reason = TargetReason::none;
};
TargetPick command_target(const Speakers &players, const QueueLookup &queues,
                          const std::string &chosen, const std::string &following,
                          const std::string &last, const std::string &own);

// Whether sending from's music to a speaker is a transfer: only music that is
// playing or paused (or stopped too, when asked), and only to an available
// speaker that is not already sounding it or playing something of its own.
bool moves_music(const Speakers &players, const QueueLookup &queues, const std::string &from,
                 const std::string &to, bool stopped);

// A group player, or a sync leader with members: its volume is the group's
// (players/cmd/group_volume), as LG sets it.
bool group_player(const SpeakerInfo &player);

// Speakers together: every real speaker, with a tick for those sounding the
// leader's music. The leader, fixed members, speakers it cannot group with,
// and every speaker when the leader cannot group, are listed but not allowed.
struct GroupOption
{
    std::string id, name;
    bool member = false, leader = false, allowed = false;
};
std::vector<GroupOption> group_options(const std::vector<CompanionPlayer> &players,
                                       const std::string &leader_id);
} // namespace hui::afterglow
