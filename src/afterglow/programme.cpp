// Afterglow - Tonight's programme: the evening's songs, set as a concert programme.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/programme.hpp"
#include "afterglow/json_slice.hpp"
#include "afterglow/presentation.hpp"
#include <algorithm>
#include <chrono>
#include <map>

namespace hui::afterglow
{
namespace js = json_slice;
namespace
{
constexpr std::size_t kLogLimit = 80;
constexpr std::int64_t kLogKeepSeconds = 12 * 3600;
constexpr int kHistoryLimit = 60;
constexpr int kQueueItemsLimit = 100;
std::string two(int value)
{
    return (value < 10 ? "0" : "") + std::to_string(value);
}
std::int64_t window_start(const LocalTime &now)
{
    return now.local - std::int64_t{kProgrammeHours} * 3600;
}
} // namespace

namespace programme_wire
{
std::string clock_label(std::int64_t local)
{
    const auto t = local_time_at(local, local);
    return two(t.hour) + ":" + two(t.minute);
}

LocalTime evening_of(const LocalTime &now)
{
    return now.hour < 5 ? local_time_at(now.utc - 86400, now.local - 86400) : now;
}

Headline headline(const LocalTime &now)
{
    Headline out;
    out.word = now.hour >= 17 || now.hour < 5 ? "Tonight" : "Today";
    const auto day = evening_of(now);
    out.date =
        std::to_string(day.day) + " " + playlist_wire::kMonths[std::clamp(day.month, 1, 12) - 1];
    return out;
}

std::string history_args(const std::string &queue_id, std::int64_t utc_now)
{
    return "{\"limit\":" + std::to_string(kHistoryLimit) + ",\"queue_id\":" + js::quote(queue_id) +
           ",\"media_types\":[\"track\"],\"fully_played_only\":false,\"played_after_timestamp\":" +
           std::to_string(utc_now - std::int64_t{kProgrammeHours} * 3600) + "}";
}

std::string queue_items_args(const std::string &queue_id)
{
    return "{\"queue_id\":" + js::quote(queue_id) +
           ",\"offset\":0,\"limit\":" + std::to_string(kQueueItemsLimit) + "}";
}

std::vector<HistorySong> decode_history(std::string_view result)
{
    std::vector<HistorySong> out;
    for (auto item : js::elements(result, kHistoryLimit))
    {
        HistorySong song;
        song.uri = js::text(item, "uri");
        song.title = js::text(item, "name");
        if (!song.uri.empty())
            out.push_back(std::move(song));
    }
    return out;
}

std::vector<ProgrammeSong> songs(const std::vector<HistorySong> *history,
                                 const std::vector<LoggedSong> &log, const std::string &queue_id,
                                 const std::vector<CompanionTrack> &queue_items,
                                 const LocalTime &now)
{
    const auto since = window_start(now);
    std::vector<const LoggedSong *> mine;
    std::map<std::string, const LoggedSong *> latest;
    for (const auto &entry : log)
        if (entry.queue_id == queue_id && entry.local >= since)
        {
            mine.push_back(&entry);
            latest[entry.uri] = &entry;
        }
    std::map<std::string, std::string> artists;
    for (const auto &item : queue_items)
        if (!item.uri.empty() && !artists.contains(item.uri))
            artists[item.uri] = item.artist;
    std::vector<ProgrammeSong> out;
    std::map<std::string, bool> seen;
    const auto add = [&](const std::string &uri, const std::string &title)
    {
        if (uri.empty() || seen[uri])
            return;
        seen[uri] = true;
        ProgrammeSong song;
        song.uri = uri;
        const auto logged = latest.find(uri);
        const LoggedSong *entry = logged == latest.end() ? nullptr : logged->second;
        song.title = !title.empty() ? title : entry != nullptr ? entry->title : "Untitled";
        if (entry != nullptr && !entry->artist.empty())
            song.artist = entry->artist;
        else if (const auto a = artists.find(uri); a != artists.end())
            song.artist = a->second;
        song.local = entry != nullptr ? entry->local : -1;
        out.push_back(std::move(song));
    };
    if (history != nullptr)
        for (auto it = history->rbegin(); it != history->rend(); ++it)
            add(it->uri, it->title);
    for (const auto *entry : mine)
        add(entry->uri, entry->title);
    return out;
}

bool offered(const std::vector<ProgrammeSong> &songs)
{
    return static_cast<int>(songs.size()) >= kProgrammeMinSongs;
}

namespace
{
std::string count_of(std::size_t n)
{
    return std::to_string(n) + (n == 1 ? " song" : " songs");
}
} // namespace

std::string summary(const std::vector<ProgrammeSong> &songs)
{
    std::int64_t first = -1, last = -1;
    for (const auto &song : songs)
        if (song.local >= 0)
        {
            first = first < 0 ? song.local : std::min(first, song.local);
            last = std::max(last, song.local);
        }
    auto out = count_of(songs.size());
    if (first < 0)
        return out;
    out += " · " + clock_label(first);
    if (last - first >= 60)
        out += " to " + clock_label(last);
    return out;
}

std::string offer_detail(const std::vector<ProgrammeSong> &songs, const std::string &queue_name)
{
    std::int64_t first = -1;
    for (const auto &song : songs)
        if (song.local >= 0)
            first = first < 0 ? song.local : std::min(first, song.local);
    const auto count = std::to_string(songs.size()) + " songs";
    if (first >= 0)
        return count + " since " + clock_label(first);
    return queue_name.empty() ? count : count + " on " + queue_name;
}

std::array<std::vector<ProgrammeRow>, 2> columns(const std::vector<ProgrammeSong> &songs)
{
    std::vector<ProgrammeRow> rows;
    for (std::size_t i = 0; i < songs.size(); ++i)
    {
        ProgrammeRow row;
        row.number = two(static_cast<int>(i) + 1);
        row.title = songs[i].title;
        row.artist = songs[i].artist;
        row.time = songs[i].local >= 0 ? clock_label(songs[i].local) : "";
        rows.push_back(std::move(row));
    }
    if (static_cast<int>(rows.size()) > kProgrammeRows)
    {
        const auto more = rows.size() - (kProgrammeRows - 1);
        rows.resize(kProgrammeRows - 1);
        ProgrammeRow last;
        last.title = "and " + std::to_string(more) + " more";
        last.more = true;
        rows.push_back(std::move(last));
    }
    std::array<std::vector<ProgrammeRow>, 2> out;
    constexpr std::size_t half = kProgrammeRows / 2;
    for (std::size_t i = 0; i < rows.size(); ++i)
        out[i < half ? 0 : 1].push_back(rows[i]);
    return out;
}
} // namespace programme_wire

void SessionLog::observe(const CompanionSnapshot &c, const LocalTime &now)
{
    const auto &track = c.track;
    if (!c.playing || c.target_elsewhere || track.uri.empty() || track.media_type != "track")
        return;
    const auto key = c.queue_id + ":" + track.queue_item_id + ":" + track.uri;
    if (key == last_)
        return;
    last_ = key;
    std::erase_if(entries_,
                  [&](const LoggedSong &e) { return now.local - e.local >= kLogKeepSeconds; });
    LoggedSong song;
    song.queue_id = c.queue_id;
    song.uri = track.uri;
    song.title = track.title;
    song.artist = track.artist;
    song.local = now.local;
    entries_.push_back(std::move(song));
    if (entries_.size() > kLogLimit)
        entries_.erase(entries_.begin(),
                       entries_.begin() + static_cast<std::ptrdiff_t>(entries_.size() - kLogLimit));
}

void Programme::refresh(const std::string &queue_id, const std::string &queue_name,
                        const LocalTime &now)
{
    const auto &entries = log_.entries();
    queue_id_ = !queue_id.empty() ? queue_id : entries.empty() ? "" : entries.back().queue_id;
    if (!queue_name.empty())
        queue_name_ = queue_name;
    if (queue_id_.empty())
        return;
    const auto stamp = queue_id_ + ":" + std::to_string(now.utc / 60);
    if (stamp == loaded_)
        return;
    loaded_ = stamp;
    history_utc_ = now.utc;
    history_wanted_ = true;
}

std::vector<ProgrammeSong> Programme::songs(const LocalTime &now) const
{
    return programme_wire::songs(history_ok_ ? &history_ : nullptr, log_.entries(), queue_id_,
                                 queue_items_, now);
}

std::optional<Programme::Tile> Programme::tile(const LocalTime &now) const
{
    if (queue_id_.empty())
        return std::nullopt;
    const auto list = songs(now);
    if (!programme_wire::offered(list))
        return std::nullopt;
    return Tile{programme_wire::headline(now).word,
                programme_wire::offer_detail(list, queue_name_)};
}

void Programme::open()
{
    open_ = true;
    focus = 0;
    queue_items_.clear();
    items_wanted_ = !queue_id_.empty();
}

ProgrammeAct Programme::press(ProgrammeKey key)
{
    switch (key)
    {
    case ProgrammeKey::up:
    case ProgrammeKey::down:
    {
        const int next = key == ProgrammeKey::up ? 0 : 1;
        if (next == focus)
            return ProgrammeAct::refused;
        focus = next;
        return ProgrammeAct::moved;
    }
    case ProgrammeKey::confirm:
        if (focus == 0)
            return ProgrammeAct::keep;
        break;
    case ProgrammeKey::back:
        break;
    }
    close();
    return ProgrammeAct::closed;
}

bool Programme::keep(Playlists &lists, const LocalTime &now) const
{
    std::vector<std::string> uris;
    for (const auto &song : songs(now))
        uris.push_back(song.uri);
    return lists.keep(std::move(uris), programme_wire::headline(now).word, kKeepKey,
                      programme_wire::evening_of(now));
}

ProgrammePage Programme::page(const LocalTime &now, const Playlists &lists) const
{
    const auto list = songs(now);
    ProgrammePage out;
    out.where = queue_name_.empty() ? "Music Assistant" : queue_name_;
    out.summary = programme_wire::summary(list);
    out.headline = programme_wire::headline(now);
    out.columns = programme_wire::columns(list);
    out.focus = focus;
    if (const auto done = lists.confirmation(kKeepKey, lists.clock()))
    {
        out.keep_value = done->text;
        out.keep_tick = done->tick;
    }
    return out;
}

void Programme::update(std::vector<ApiReply> &replies, std::vector<ApiCall> &calls,
                       std::uint64_t &next_id)
{
    for (auto it = replies.begin(); it != replies.end();)
    {
        if (history_waiting_ != 0 && it->id == history_waiting_)
        {
            history_waiting_ = 0;
            history_ok_ = it->error_code == 0;
            history_ = history_ok_ ? programme_wire::decode_history(it->result)
                                   : std::vector<HistorySong>{};
            it = replies.erase(it);
        }
        else if (items_waiting_ != 0 && it->id == items_waiting_)
        {
            items_waiting_ = 0;
            // The log's artists are enough when the queue can't be read.
            if (it->error_code == 0 && open_)
                queue_items_ = companion_wire::decode_items(it->result, kQueueItemsLimit);
            it = replies.erase(it);
        }
        else
            ++it;
    }
    const auto send = [&](const char *command, std::string args, std::uint64_t &waiting)
    {
        ApiCall call;
        call.id = ++next_id;
        call.command = command;
        call.args = std::move(args);
        waiting = call.id;
        calls.push_back(std::move(call));
    };
    if (std::exchange(history_wanted_, false))
        send("music/recently_played_items", programme_wire::history_args(queue_id_, history_utc_),
             history_waiting_);
    if (std::exchange(items_wanted_, false))
        send("player_queues/items", programme_wire::queue_items_args(queue_id_), items_waiting_);
}

void pump_playlists(Presentation &model, std::vector<ApiReply> replies,
                    const std::function<void(const ApiCall &)> &send)
{
    const auto now = local_time();
    model.programme.observe(model.companion, now);
    std::vector<ApiCall> calls;
    const auto clock = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    model.playlists.update(clock, replies, calls, model.api_next);
    model.programme.update(replies, calls, model.api_next);
    for (const auto &call : calls)
        send(call);
    if (auto notice = model.playlists.take_notice(); !notice.empty())
        model.show_toast(std::move(notice));
}
} // namespace hui::afterglow
