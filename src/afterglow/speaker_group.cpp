// Afterglow - Joining the music elsewhere, what each speaker plays, one speaker's volume.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/speaker_group.hpp"
#include <algorithm>

namespace hui::afterglow
{
namespace
{
bool sounds(const QueueInfo *queue)
{
    return queue != nullptr && queue->has_item &&
           (queue->state == "playing" || queue->state == "paused");
}
} // namespace

std::string speaker_activity(const std::string &player_id, const Speakers &players,
                             const QueueLookup &queues)
{
    const auto id = audible_queue(player_id, players, queues);
    const QueueInfo *queue = id.empty() ? nullptr : queues(id);
    if (!sounds(queue))
        return "Idle";
    const std::string title = queue->title.empty() ? "Unknown track" : queue->title;
    const std::string song = queue->artist.empty() ? title : title + " — " + queue->artist;
    return queue->state == "paused" ? "Paused · " + song : song;
}

JoinPlan join_plan(const std::vector<CompanionPlayer> &list, const Speakers &players,
                   const QueueLookup &queues, const std::string &leader, const std::string &own)
{
    if (own.empty() || leader.empty() || own == leader || !sounds(queues(leader)))
        return {};
    const auto mine = players.find(own);
    if (mine == players.end() || !mine->second.available || !mine->second.enabled)
        return {};
    const auto sounding = audible_queue(own, players, queues);
    if (!sounding.empty())
    {
        const QueueInfo *queue = queues(sounding);
        if (sounding == leader || (queue != nullptr && queue->state == "playing"))
            return {};
    }
    const auto options = group_options(list, leader);
    const auto option = std::find_if(options.begin(), options.end(),
                                     [&](const GroupOption &o) { return o.id == own; });
    if (option == options.end() || !option->allowed || option->member)
        return {};
    return {leader, own};
}

std::string leader_name(const Speakers &players, const std::string &leader)
{
    const auto it = players.find(leader);
    return it != players.end() && !it->second.name.empty() ? it->second.name : leader;
}

void describe_speakers(std::vector<CompanionPlayer> &list, const Speakers &players,
                       const QueueLookup &queues)
{
    for (auto &player : list)
    {
        player.activity = speaker_activity(player.id, players, queues);
        const auto it = players.find(player.id);
        const bool known = it != players.end();
        player.volume_settable = known && it->second.volume_settable;
        player.volume = known && it->second.volume_settable ? it->second.volume : -1;
    }
}

std::vector<MemberVolume> member_volumes(const std::vector<CompanionPlayer> &list,
                                         const std::string &leader)
{
    const auto options = group_options(list, leader);
    const auto members = std::count_if(options.begin(), options.end(),
                                       [](const GroupOption &o) { return o.member; });
    std::vector<MemberVolume> out;
    if (members < 2)
        return out;
    for (const auto &option : options)
    {
        if (!option.member)
            continue;
        const auto player = std::find_if(list.begin(), list.end(), [&](const CompanionPlayer &p)
                                         { return p.id == option.id; });
        if (player == list.end() || !player->volume_settable || player->volume < 0)
            continue;
        MemberVolume volume;
        volume.id = option.id;
        volume.name = option.name;
        volume.level = player->volume;
        out.push_back(std::move(volume));
    }
    return out;
}
} // namespace hui::afterglow
