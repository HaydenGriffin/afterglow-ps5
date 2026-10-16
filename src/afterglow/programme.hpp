// Afterglow - Tonight's programme: the evening's songs, set as a concert programme.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/companion.hpp"
#include "afterglow/playlists.hpp"
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// LG Afterglow's programme.js / programme-view.js (spec:
// .lanes/_brief/R2-playlists.md). The songs the queue played in the last
// four hours: Music Assistant's played history for the queue
// (music/recently_played_items with queue_id and played_after_timestamp)
// survives a restart but carries no artists or times; a session log kept
// while the app runs adds both, and the queue's own items add artists for
// songs the log missed. The idle poster offers it; it never opens by itself.
namespace hui::afterglow
{
constexpr int kProgrammeHours = 4;
constexpr int kProgrammeMinSongs = 3;
constexpr int kProgrammeRows = 22; // two columns of eleven

struct LoggedSong
{
    std::string queue_id, uri, title, artist;
    std::int64_t local = 0; // LocalTime::local when it started
};
struct HistorySong
{
    std::string uri, title;
};
struct ProgrammeSong
{
    std::string uri, title, artist;
    std::int64_t local = -1; // start, -1 when not known
};
struct ProgrammeRow
{
    std::string number, title, artist, time;
    bool more = false;
};
struct Headline
{
    std::string word, date; // "Tonight" / "Today", "16 October"
};

namespace programme_wire
{
// "21:04" for a LocalTime::local value.
std::string clock_label(std::int64_t local);
// Tonight from 17:00 to 05:00; before 05:00 the evening is the day before.
Headline headline(const LocalTime &now);
std::string history_args(const std::string &queue_id, std::int64_t utc_now);
std::string queue_items_args(const std::string &queue_id);
std::vector<HistorySong> decode_history(std::string_view result);
// History (newest first, one row per song) reversed, then songs only the log
// knows in the order they started; artist and time from the log, else
// artists from the queue's items. history null: the log stands alone.
std::vector<ProgrammeSong> songs(const std::vector<HistorySong> *history,
                                 const std::vector<LoggedSong> &log, const std::string &queue_id,
                                 const std::vector<CompanionTrack> &queue_items,
                                 const LocalTime &now);
bool offered(const std::vector<ProgrammeSong> &songs);
// "14 songs · 19:40 to 22:15", or the count alone.
std::string summary(const std::vector<ProgrammeSong> &songs);
// The idle tile's line: "14 songs since 19:40", else "14 songs on <queue>".
std::string offer_detail(const std::vector<ProgrammeSong> &songs, const std::string &queue_name);
// Two columns of eleven; past 22 songs the last row says how many more.
std::array<std::vector<ProgrammeRow>, 2> columns(const std::vector<ProgrammeSong> &songs);
} // namespace programme_wire

// Songs as they start on the queue the screen follows (80 entries, 12 h).
class SessionLog
{
  public:
    void observe(const CompanionSnapshot &c, const LocalTime &now);
    const std::vector<LoggedSong> &entries() const
    {
        return entries_;
    }

  private:
    std::vector<LoggedSong> entries_;
    std::string last_;
};

// The programme's data and its page's state; pumped every frame by
// pump_playlists (main.cpp), drawn by draw_programme (scenes).
class Programme
{
  public:
    static constexpr const char *kKeepKey = "programme";
    struct Tile
    {
        std::string word, detail;
    };
    void observe(const CompanionSnapshot &c, const LocalTime &now)
    {
        log_.observe(c, now);
    }
    // The idle poster appeared: read the queue's history (once a minute at most).
    void refresh(const std::string &queue_id, const std::string &queue_name, const LocalTime &now);
    std::optional<Tile> tile(const LocalTime &now) const;
    std::vector<ProgrammeSong> songs(const LocalTime &now) const;
    // Opening reads the queue's items for artists; Back closes it.
    void open();
    void close()
    {
        open_ = false;
    }
    bool is_open() const
    {
        return open_;
    }
    const std::string &queue_name() const
    {
        return queue_name_;
    }
    int focus = 0; // 0 Keep it as a playlist, 1 Back
    void update(std::vector<ApiReply> &replies, std::vector<ApiCall> &calls,
                std::uint64_t &next_id);
    const SessionLog &log() const
    {
        return log_;
    }

  private:
    SessionLog log_;
    std::string queue_id_, queue_name_, loaded_;
    std::vector<HistorySong> history_;
    bool history_ok_ = true, history_wanted_ = false, items_wanted_ = false;
    std::int64_t history_utc_ = 0;
    std::uint64_t history_waiting_ = 0, items_waiting_ = 0;
    std::vector<CompanionTrack> queue_items_;
    bool open_ = false;
};

struct Presentation;
// Every frame (main.cpp): notes the playing song in the session log, hands
// Music Assistant's replies to the playlist and programme flows, sends the
// calls they want and shows a finished job's words as a toast.
void pump_playlists(Presentation &model, std::vector<ApiReply> replies,
                    const std::function<void(const ApiCall &)> &send);
} // namespace hui::afterglow
