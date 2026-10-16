// Afterglow - Which lyrics the song on screen has: Music Assistant's, else LRCLIB's.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/lrclib.hpp"
#include "afterglow/song_store.hpp"
#include <cstdint>
#include <deque>
#include <string>

namespace hui::afterglow
{
// LG playback.js lyrics.status.
enum class LyricsStatus
{
    idle,    // no song
    loading, // asking Music Assistant, or LRCLIB
    timed,
    plain,
    missing, // a clean answer from every source: no lyrics
    error    // a source failed; retryable
};
struct LyricsState
{
    LyricsStatus status = LyricsStatus::idle;
    // While status is error: when the one automatic retry happens (ms on the
    // lookup's clock), or 0 when it has been used.
    std::int64_t retry_at_ms = 0;
};

// Music Assistant's answer to metadata/get_track_lyrics for a song.
enum class MaLyrics
{
    asked,
    answered, // plain and lrc hold its text, possibly none
    failed
};
struct LyricsSong
{
    // Changes when another song (or queue item) comes on; empty for none.
    std::string key;
    // The song's own identity, for the small cache of answers.
    std::string uri;
    // Radio streams and podcasts have no lyrics to look up.
    bool track = false;
    lrclib::Query query;
    MaLyrics ma = MaLyrics::asked;
    // Which request ma answers; a retry waits for a later one.
    std::uint64_t ma_request = 0;
    std::string ma_plain, ma_lrc;
    // The LRCLIB record chosen for the song (song_store.hpp), set only while
    // Online lyrics is on: fetched instead of asking anyone else.
    std::int64_t chosen = 0;
};

// The order of LG Afterglow's lyrics loading, without its threads:
//  1. Music Assistant's lyrics, given 5 s.
//  2. With none, after an error or past the 5 s, and Online lyrics on, an
//     LRCLIB search (only while the lyrics are on screen).
//  3. Only a clean answer from both is "missing"; any failure is "error",
//     retried once automatically after 30 s and by hand at any time (which
//     also forgets a remembered "missing").
// The last three songs' results are kept so going back does not ask again.
class LyricsLookup
{
  public:
    static constexpr std::int64_t kMaWaitMs = 5000;
    static constexpr std::int64_t kRetryDelayMs = 30000;
    enum class SearchOutcome
    {
        found,
        none,
        failed
    };

    // Once per status tick. wanted: lyrics are on screen; online: the
    // Online lyrics setting.
    void update(const LyricsSong &song, bool wanted, bool online, std::int64_t now_ms);
    // Retry lyrics: asks every source again.
    void retry();
    // A search to run (once per ticket).
    bool take_search(lrclib::Query &query, std::uint64_t &ticket);
    // Ask Music Assistant again (a retry), once.
    bool take_ma_retry();
    // A search's answer; one for a ticket no longer wanted is ignored.
    void search_done(std::uint64_t ticket, SearchOutcome outcome, const std::string &plain,
                     const std::string &lrc);
    LyricsState state() const;
    LyricsSource source() const
    {
        return source_;
    }
    // Choose other lyrics: show these now, for this song.
    void use(const std::string &plain, const std::string &lrc);
    // True once after the chosen record turned out gone from LRCLIB (the
    // usual lookup then runs): the choice should be forgotten.
    bool take_choice_gone();
    const std::string &plain() const
    {
        return plain_;
    }
    const std::string &lrc() const
    {
        return lrc_;
    }

  private:
    enum class Phase
    {
        idle,
        waiting,   // for Music Assistant
        to_search, // waiting to be wanted
        searching,
        done
    };
    struct Cached
    {
        std::string uri, plain, lrc;
        LyricsStatus status = LyricsStatus::missing;
        LyricsSource source = LyricsSource::none;
    };
    void start(const LyricsSong &song, std::int64_t now_ms, bool again);
    void finish(std::string plain, std::string lrc, bool failed, LyricsSource source);
    void fail();
    Phase phase_ = Phase::idle;
    LyricsStatus status_ = LyricsStatus::idle;
    std::string key_, uri_, plain_, lrc_;
    lrclib::Query query_;
    std::int64_t since_ms_ = 0, retry_at_ms_ = 0, now_ms_ = 0;
    std::uint64_t ma_after_ = 0, ticket_ = 0, issued_ = 0;
    int retries_ = 0;
    bool ma_failed_ = false, retry_wanted_ = false, ask_ma_ = false, search_due_ = false;
    bool getting_ = false, choice_gone_ = false;
    LyricsSource source_ = LyricsSource::none;
    std::deque<Cached> cache_;
};
} // namespace hui::afterglow
