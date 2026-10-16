// Afterglow - Which lyrics the song on screen has: Music Assistant's, else LRCLIB's.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/lyrics_lookup.hpp"
#include "afterglow/lyrics.hpp"
#include <algorithm>

namespace hui::afterglow
{
void LyricsLookup::update(const LyricsSong &song, bool wanted, bool online, std::int64_t now_ms)
{
    now_ms_ = now_ms;
    if (song.key.empty())
    {
        if (phase_ != Phase::idle)
        {
            phase_ = Phase::idle;
            status_ = LyricsStatus::idle;
            key_.clear();
            plain_.clear();
            lrc_.clear();
            search_due_ = false;
        }
        return;
    }
    if (song.key != key_)
    {
        key_ = song.key;
        retries_ = 0;
        retry_wanted_ = false;
        start(song, now_ms, false);
    }
    else if (retry_wanted_)
    {
        retry_wanted_ = false;
        start(song, now_ms, true);
    }
    else if (wanted && phase_ == Phase::done && status_ == LyricsStatus::error &&
             retry_at_ms_ != 0 && now_ms >= retry_at_ms_)
    {
        ++retries_;
        start(song, now_ms, true);
    }

    if (phase_ == Phase::waiting)
    {
        if (song.ma != MaLyrics::asked && song.ma_request > ma_after_)
        {
            if (song.ma == MaLyrics::answered && (!song.ma_plain.empty() || !song.ma_lrc.empty()))
            {
                finish(song.ma_plain, song.ma_lrc, false);
                return;
            }
            ma_failed_ = song.ma == MaLyrics::failed;
            phase_ = Phase::to_search;
        }
        else if (now_ms - since_ms_ >= kMaWaitMs)
        {
            // A provider can keep Music Assistant retrying well past its own
            // timeout; the search need not wait for all of that.
            ma_failed_ = true;
            phase_ = Phase::to_search;
        }
    }
    if (phase_ == Phase::to_search)
    {
        if (!online || lrclib::search_path(query_).empty())
            finish({}, {}, ma_failed_);
        else if (wanted)
        {
            phase_ = Phase::searching;
            ticket_ = ++issued_;
            search_due_ = true;
        }
    }
}

void LyricsLookup::retry()
{
    cache_.erase(std::remove_if(cache_.begin(), cache_.end(),
                                [&](const Cached &c) { return c.uri == uri_; }),
                 cache_.end());
    retry_wanted_ = !key_.empty();
}

bool LyricsLookup::take_search(lrclib::Query &query, std::uint64_t &ticket)
{
    if (!search_due_ || phase_ != Phase::searching)
        return false;
    search_due_ = false;
    query = query_;
    ticket = ticket_;
    return true;
}

bool LyricsLookup::take_ma_retry()
{
    const bool ask = ask_ma_;
    ask_ma_ = false;
    return ask;
}

void LyricsLookup::search_done(std::uint64_t ticket, SearchOutcome outcome,
                               const std::string &plain, const std::string &lrc)
{
    if (phase_ != Phase::searching || ticket != ticket_)
        return;
    if (outcome == SearchOutcome::found)
        finish(plain, lrc, false);
    else if (outcome == SearchOutcome::none)
        // Only a clean answer from both sources is "missing".
        finish({}, {}, ma_failed_);
    else
        fail();
}

LyricsState LyricsLookup::state() const
{
    LyricsState out;
    out.status = status_;
    out.retry_at_ms = status_ == LyricsStatus::error ? retry_at_ms_ : 0;
    return out;
}

void LyricsLookup::start(const LyricsSong &song, std::int64_t now_ms, bool again)
{
    uri_ = song.uri;
    query_ = song.query;
    plain_.clear();
    lrc_.clear();
    ticket_ = 0;
    search_due_ = false;
    retry_at_ms_ = 0;
    ma_failed_ = false;
    if (!song.track)
    {
        phase_ = Phase::done;
        status_ = LyricsStatus::missing;
        return;
    }
    const auto cached = std::find_if(cache_.begin(), cache_.end(),
                                     [&](const Cached &c) { return c.uri == song.uri; });
    if (cached != cache_.end())
    {
        phase_ = Phase::done;
        status_ = cached->status;
        plain_ = cached->plain;
        lrc_ = cached->lrc;
        return;
    }
    phase_ = Phase::waiting;
    status_ = LyricsStatus::loading;
    since_ms_ = now_ms;
    // Asking again: Music Assistant's earlier answer no longer counts.
    ma_after_ = again ? song.ma_request : 0;
    ask_ma_ = again;
}

void LyricsLookup::finish(std::string plain, std::string lrc, bool failed)
{
    if (failed && plain.empty() && lrc.empty())
    {
        fail();
        return;
    }
    const bool timed = !parse_lyrics(lrc, true).empty();
    if (!timed)
        lrc.clear();
    status_ = timed            ? LyricsStatus::timed
              : !plain.empty() ? LyricsStatus::plain
                               : LyricsStatus::missing;
    plain_ = std::move(plain);
    lrc_ = std::move(lrc);
    phase_ = Phase::done;
    retry_at_ms_ = 0;
    cache_.erase(std::remove_if(cache_.begin(), cache_.end(),
                                [&](const Cached &c) { return c.uri == uri_; }),
                 cache_.end());
    if (uri_.empty())
        return;
    Cached entry;
    entry.uri = uri_;
    entry.plain = plain_;
    entry.lrc = lrc_;
    entry.status = status_;
    cache_.push_back(std::move(entry));
    while (cache_.size() > 3)
        cache_.pop_front();
}

void LyricsLookup::fail()
{
    phase_ = Phase::done;
    status_ = LyricsStatus::error;
    plain_.clear();
    lrc_.clear();
    retry_at_ms_ = retries_ == 0 ? now_ms_ + kRetryDelayMs : 0;
}
} // namespace hui::afterglow
