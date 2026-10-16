// Afterglow - Which speaker commands act on, moving music between speakers, grouping.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/speakers.hpp"
#include <algorithm>
#include <cctype>

namespace hui::afterglow
{
namespace
{
bool contains(const std::vector<std::string> &list, const std::string &value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}
std::string lower(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
bool usable(const SpeakerInfo *player)
{
    return player != nullptr && player->available && player->enabled;
}
const SpeakerInfo *find(const Speakers &players, const std::string &id)
{
    const auto it = players.find(id);
    return it == players.end() ? nullptr : &it->second;
}
// The queue a command for this player reaches: what it sounds now, else its
// own (idle) queue, which Play then starts.
std::string queue_of(const std::string &player_id, const Speakers &players,
                     const QueueLookup &queues)
{
    if (player_id.empty() || !usable(find(players, player_id)))
        return {};
    auto queue = audible_queue(player_id, players, queues);
    if (queue.empty() && queues(player_id) != nullptr)
        queue = player_id;
    return queue;
}
} // namespace

std::string audible_queue(const std::string &player_id, const Speakers &players,
                          const QueueLookup &queues)
{
    std::string id = player_id;
    // A loop in synced_to/active_group ends here rather than spinning.
    for (int hops = 0; hops < 32 && !id.empty(); ++hops)
    {
        const auto *player = find(players, id);
        if (player == nullptr || !player->available)
            return {};
        if (!player->synced_to.empty() && player->synced_to != id)
        {
            id = player->synced_to;
            continue;
        }
        if (!player->active_group.empty() && player->active_group != id)
        {
            id = player->active_group;
            continue;
        }
        const auto source = player->active_source.empty() ? id : player->active_source;
        return queues(source) != nullptr ? source : std::string{};
    }
    return {};
}

TargetPick command_target(const Speakers &players, const QueueLookup &queues,
                          const std::string &chosen, const std::string &following,
                          const std::string &last, const std::string &own)
{
    const auto pick = [&](const std::string &player_id, TargetReason reason)
    {
        TargetPick out;
        out.queue_id = queue_of(player_id, players, queues);
        if (!out.queue_id.empty())
        {
            out.player_id = player_id;
            out.reason = reason;
        }
        return out;
    };
    if (auto out = pick(chosen, TargetReason::chosen); !out.queue_id.empty())
        return out;
    if (!following.empty() && queues(following) != nullptr)
    {
        TargetPick out;
        out.queue_id = out.player_id = following;
        out.reason = TargetReason::following;
        return out;
    }
    if (auto out = pick(last, TargetReason::last); !out.queue_id.empty())
        return out;
    if (auto out = pick(own, TargetReason::this_ps5); !out.queue_id.empty())
        return out;
    // The first speaker by name, real speakers before groups (LG speakersOf).
    std::vector<const SpeakerInfo *> ordered;
    for (const auto &[id, player] : players)
        if ((player.type == "player" || player.type == "stereo_pair" || player.type == "group") &&
            usable(&player) && !player.hidden)
            ordered.push_back(&player);
    std::sort(ordered.begin(), ordered.end(),
              [](const SpeakerInfo *a, const SpeakerInfo *b)
              {
                  const bool ga = a->type == "group", gb = b->type == "group";
                  if (ga != gb)
                      return gb;
                  const auto na = lower(a->name.empty() ? a->id : a->name);
                  const auto nb = lower(b->name.empty() ? b->id : b->name);
                  return na != nb ? na < nb : a->id < b->id;
              });
    for (const auto *player : ordered)
        if (auto out = pick(player->id, TargetReason::first); !out.queue_id.empty())
            return out;
    return {};
}

bool moves_music(const Speakers &players, const QueueLookup &queues, const std::string &from,
                 const std::string &to, bool stopped)
{
    const auto *source = from.empty() ? nullptr : queues(from);
    if (source == nullptr || !source->has_item || to.empty() || to == from)
        return false;
    if (!stopped && source->state != "playing" && source->state != "paused")
        return false;
    if (!usable(find(players, to)))
        return false;
    const auto sounding = audible_queue(to, players, queues);
    if (sounding.empty())
        return true;
    const auto *queue = queues(sounding);
    return sounding != from && (queue == nullptr || queue->state != "playing");
}

bool group_player(const SpeakerInfo &player)
{
    if (player.type == "group")
        return true;
    return std::any_of(player.group_members.begin(), player.group_members.end(),
                       [&](const std::string &id) { return id != player.id; });
}

std::vector<GroupOption> group_options(const std::vector<CompanionPlayer> &players,
                                       const std::string &leader_id)
{
    const auto leader = std::find_if(players.begin(), players.end(),
                                     [&](const auto &p) { return p.id == leader_id; });
    if (leader_id.empty() || leader == players.end())
        return {};
    std::vector<GroupOption> options;
    for (const auto &player : players)
    {
        if (player.type == "group")
            continue;
        GroupOption option;
        option.id = player.id;
        option.name = player.name;
        option.leader = player.id == leader_id;
        option.member = option.leader || contains(leader->group_members, player.id);
        // can_group_with names players, or whole providers whose players all qualify.
        const bool compatible =
            option.member || contains(leader->can_group_with, player.id) ||
            (!player.provider.empty() && contains(leader->can_group_with, player.provider));
        option.allowed = leader->can_set_members && !option.leader && player.available &&
                         !contains(leader->static_members, player.id) && compatible;
        options.push_back(std::move(option));
    }
    return options;
}
} // namespace hui::afterglow
