// Afterglow - The idle poster: what was played, set as a poster while nothing plays.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/idle.hpp"
#include <algorithm>
#include <cmath>

namespace hui::afterglow
{
namespace
{
const char *kind_label(const std::string &type)
{
    if (type == "track")
        return "Song";
    if (type == "album")
        return "Album";
    if (type == "playlist")
        return "Playlist";
    if (type == "artist")
        return "Artist";
    if (type == "radio")
        return "Radio";
    if (type == "audiobook")
        return "Audiobook";
    if (type == "podcast")
        return "Podcast";
    if (type == "podcast_episode")
        return "Episode";
    return "";
}
// LG browse-view.js detailOf: what it is, who made it, and a year or length.
std::vector<std::string> detail_parts(const CompanionTrack &item)
{
    std::vector<std::string> parts;
    if (*kind_label(item.media_type) != '\0')
        parts.emplace_back(kind_label(item.media_type));
    if (!item.artist.empty())
        parts.push_back(item.artist);
    if (item.media_type == "album" && !item.year.empty())
        parts.push_back(item.year);
    if (item.media_type == "track" && item.duration > 0)
        parts.push_back(std::to_string(item.duration / 60) + ":" +
                        (item.duration % 60 < 10 ? "0" : "") + std::to_string(item.duration % 60));
    return parts;
}
std::string joined(const std::vector<std::string> &parts)
{
    std::string out;
    for (const auto &part : parts)
        out += (out.empty() ? "" : " · ") + part;
    return out;
}
} // namespace

std::vector<IdleTile> idle_tiles(const CompanionTrack *paused,
                                 const std::vector<CompanionTrack> &recent, RecentState state,
                                 bool setup)
{
    std::vector<IdleTile> tiles;
    if (paused != nullptr && !paused->title.empty())
    {
        IdleTile tile;
        tile.kind = IdleTileKind::resume;
        tile.item = *paused;
        tiles.push_back(std::move(tile));
    }
    if (state == RecentState::error)
    {
        IdleTile tile;
        tile.kind = IdleTileKind::retry;
        tiles.push_back(std::move(tile));
    }
    int added = 0;
    for (const auto &item : recent)
    {
        if (item.uri.empty() || (paused != nullptr && item.uri == paused->uri))
            continue;
        if (added++ >= companion_wire::kRecentLimit)
            break;
        IdleTile tile;
        tile.kind = IdleTileKind::play;
        tile.item = item;
        tiles.push_back(std::move(tile));
    }
    tiles.emplace_back();
    const bool nothing = paused == nullptr || paused->title.empty();
    if (setup && nothing &&
        (state == RecentState::error || (state == RecentState::ready && tiles.size() == 1)))
    {
        IdleTile tile;
        tile.kind = IdleTileKind::setup;
        tiles.push_back(std::move(tile));
    }
    return tiles;
}

IdleText idle_text(const IdleTile &tile, RecentState state, bool has_recent)
{
    switch (tile.kind)
    {
    case IdleTileKind::resume:
    {
        // "Paused" stands where the kind would be.
        auto parts = detail_parts(tile.item);
        if (!parts.empty() && parts.front() == kind_label(tile.item.media_type))
            parts.erase(parts.begin());
        parts.insert(parts.begin(), "Paused");
        return {tile.item.title, joined(parts), "Resume"};
    }
    case IdleTileKind::play:
        return {tile.item.title.empty() ? "Untitled" : tile.item.title,
                joined(detail_parts(tile.item)), "Play"};
    case IdleTileKind::retry:
        return {"Nothing playing", "Couldn’t load what you played", "Try again"};
    case IdleTileKind::setup:
        return {"Nothing playing", "Something not right? Check my setup", "Check your setup"};
    case IdleTileKind::browse:
        break;
    }
    if (state == RecentState::loading || state == RecentState::idle)
        return {"Nothing playing", "Finding what you played…", "Open Browse"};
    if (!has_recent)
        return {"Nothing playing", "Play something in Music Assistant and it shows here",
                "Open Browse"};
    return {"Browse", "Search, favourites, playlists and radio", "Open Browse"};
}

void IdlePoster::show(RecentState recent, bool &load)
{
    showing_ = true;
    since_key_ = 0;
    focus_ = 0;
    note_.clear();
    starting_ = false;
    level_ = 1;
    if (recent != RecentState::ready || since_load_ < 0 || since_load_ > kRecentReloadSeconds)
    {
        load = true;
        since_load_ = 0;
    }
}

bool IdlePoster::update(float dt, const Inputs &in)
{
    if (in.playing)
        quiet_ = -1;
    else
        quiet_ = quiet_ < 0 ? 0 : quiet_ + dt;
    if (since_load_ >= 0)
        since_load_ += dt;
    const float wait = in.paused ? kPausedIdleAfterSeconds : kIdleAfterSeconds;
    const bool want = in.allowed && quiet_ >= wait;
    bool load = false;
    if (want && !showing_)
        show(in.recent, load);
    else if (!want && showing_)
    {
        showing_ = false;
        starting_ = false;
        note_.clear();
    }
    if (!showing_)
        return load;
    since_key_ += dt;
    focus_ = std::clamp(focus_, 0, std::max(0, in.tiles - 1));
    note_age_ += dt;
    if (starting_ && note_age_ > kIdleStartSeconds)
        failed();
    const float target = resting() ? kRestLevel : 1.0f;
    level_ = reduced_motion_ ? target : level_ + (target - level_) * (1 - std::exp(-dt * 2.0f));
    return load;
}

bool IdlePoster::press(IdleKey key, const std::vector<IdleTile> &tiles, IdleAct &act)
{
    act = IdleAct::none;
    if (!showing_)
        return false;
    const bool was_resting = resting();
    since_key_ = 0;
    if (key == IdleKey::other)
        return false;
    if (was_resting)
        return true;
    const int last = static_cast<int>(tiles.size()) - 1;
    if (key == IdleKey::left || key == IdleKey::right)
    {
        focus_ = std::clamp(focus_ + (key == IdleKey::left ? -1 : 1), 0, std::max(0, last));
        return true;
    }
    if (tiles.empty())
        return true;
    switch (tiles[static_cast<std::size_t>(std::clamp(focus_, 0, last))].kind)
    {
    case IdleTileKind::resume:
        act = IdleAct::resume;
        break;
    case IdleTileKind::play:
        act = IdleAct::play;
        break;
    case IdleTileKind::retry:
        act = IdleAct::retry;
        since_load_ = 0;
        break;
    case IdleTileKind::browse:
        act = IdleAct::browse;
        break;
    case IdleTileKind::setup:
        act = IdleAct::setup;
        break;
    }
    return true;
}

void IdlePoster::starting(const char *note)
{
    note_ = note;
    note_age_ = 0;
    starting_ = true;
}

void IdlePoster::failed()
{
    note_ = "Couldn’t start it. Cross tries again";
    note_age_ = 0;
    starting_ = false;
}
} // namespace hui::afterglow
