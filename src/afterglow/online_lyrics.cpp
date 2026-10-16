// Afterglow - LRCLIB searches on a worker thread, and the per-tick lyrics pump.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/online_lyrics.hpp"
#include "afterglow/presentation.hpp"
#include "afterglow/song_lyrics.hpp"
#include "platform/ps5/system.hpp"
#include <unistd.h>

namespace hui::afterglow
{
OnlineLyrics::OnlineLyrics(LyricsFetch fetch, std::function<void()> abort)
    : fetch_(std::move(fetch)), abort_(std::move(abort))
{
}
OnlineLyrics::~OnlineLyrics()
{
    stop();
    join();
}
bool OnlineLyrics::start()
{
    if (started_ || !fetch_)
        return false;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
        return false;
    stopping_ = false;
    // BearSSL's contexts and the I/O buffer live on the heap; this is room
    // for its certificate decoding and the JSON reader.
    const int stack = pthread_attr_setstacksize(&attr, 512 * 1024);
    started_ = stack == 0 && pthread_create(&thread_, &attr, entry, this) == 0;
    pthread_attr_destroy(&attr);
    if (!started_)
        stopping_ = true;
    return started_;
}
void OnlineLyrics::search(std::uint64_t ticket, const lrclib::Query &query)
{
    std::lock_guard<std::mutex> lock(mutex_);
    request_ticket_ = ticket;
    request_ = query;
    has_request_ = true;
    wanted_ = ticket;
}
bool OnlineLyrics::take_result(std::uint64_t &ticket, LyricsLookup::SearchOutcome &outcome,
                               std::string &plain, std::string &lrc)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_result_)
        return false;
    has_result_ = false;
    ticket = result_ticket_;
    outcome = outcome_;
    plain = std::move(plain_);
    lrc = std::move(lrc_);
    return true;
}
void OnlineLyrics::find_candidates(std::uint64_t ticket, const lrclib::Query &query)
{
    std::lock_guard<std::mutex> lock(mutex_);
    candidates_ticket_ = ticket;
    candidates_request_ = query;
    has_candidates_request_ = true;
    candidates_wanted_ = ticket;
}
bool OnlineLyrics::take_candidates(std::uint64_t &ticket, bool &failed,
                                   std::vector<lrclib::Candidate> &candidates)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_candidates_)
        return false;
    has_candidates_ = false;
    ticket = candidates_result_ticket_;
    failed = candidates_failed_;
    candidates = std::move(candidates_);
    return true;
}
std::string OnlineLyrics::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
void OnlineLyrics::stop()
{
    stopping_ = true;
    if (started_ && abort_)
        abort_();
}
void OnlineLyrics::join()
{
    if (started_)
    {
        pthread_join(thread_, nullptr);
        started_ = false;
    }
}
void *OnlineLyrics::entry(void *self)
{
    static_cast<OnlineLyrics *>(self)->run();
    return nullptr;
}
void OnlineLyrics::run()
{
    while (!stopping_)
    {
        std::uint64_t ticket = 0, candidates = 0;
        lrclib::Query query;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (has_request_)
            {
                has_request_ = false;
                ticket = request_ticket_;
                query = request_;
            }
            else if (has_candidates_request_)
            {
                has_candidates_request_ = false;
                candidates = candidates_ticket_;
                query = candidates_request_;
            }
        }
        if (ticket != 0)
            answer(ticket, query);
        else if (candidates != 0)
            answer_candidates(candidates, query);
        else
            usleep(50000);
    }
}
void OnlineLyrics::answer(std::uint64_t ticket, const lrclib::Query &query)
{
    using Outcome = LyricsLookup::SearchOutcome;
    const bool get = query.record > 0;
    const auto path = get ? lrclib::get_path(query.record) : lrclib::search_path(query);
    Outcome outcome = Outcome::none;
    std::string plain, lrc, status;
    if (path.empty())
        status = "Online lyrics: nothing to search for";
    else if (get)
    {
        // The lyrics chosen for this song: 404 means LRCLIB no longer has them.
        const auto result =
            fetch_(path, [&] { return stopping_.load() || wanted_.load() != ticket; });
        lrclib::Record record;
        if (result.ok && result.status == 404)
            status = "Chosen lyrics: gone from LRCLIB";
        else if (!result.ok || result.status != 200)
        {
            outcome = Outcome::failed;
            status = "Chosen lyrics unavailable (" +
                     (result.ok ? std::to_string(result.status) : result.error) + ")";
        }
        else if (!lrclib::parse_record(result.body, record))
        {
            outcome = Outcome::failed;
            status = "Chosen lyrics: unreadable answer";
        }
        else if (!record.instrumental && (!record.synced.empty() || !record.plain.empty()))
        {
            outcome = Outcome::found;
            plain = record.plain;
            lrc = record.synced;
            status = "Chosen lyrics found";
        }
        else
            status = "Chosen lyrics: no words";
    }
    else
    {
        const auto result =
            fetch_(path, [&] { return stopping_.load() || wanted_.load() != ticket; });
        std::vector<lrclib::Record> records;
        if (!result.ok)
        {
            outcome = Outcome::failed;
            status = "Online lyrics failed: " + (result.error.empty() ? "no answer" : result.error);
        }
        else if (result.status != 200)
        {
            // 429 (rate limited) and server errors are worth another try.
            outcome = Outcome::failed;
            status = "Online lyrics unavailable (" + std::to_string(result.status) + ")";
        }
        else if (!lrclib::parse_results(result.body, records))
        {
            outcome = Outcome::failed;
            status = "Online lyrics: unreadable answer";
        }
        else if (const auto found = lrclib::pick(records, query))
        {
            outcome = Outcome::found;
            plain = found->plain;
            lrc = found->lrc;
            status = std::string("Online lyrics found (") + (lrc.empty() ? "plain" : "timed") +
                     ", " + std::to_string(records.size()) + " results)";
        }
        else
            status = "Online lyrics: no match in " + std::to_string(records.size()) + " results";
    }
    sys::log("[Afterglow] %s", status.c_str());
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = status;
    result_ticket_ = ticket;
    outcome_ = outcome;
    plain_ = std::move(plain);
    lrc_ = std::move(lrc);
    has_result_ = true;
}

void OnlineLyrics::answer_candidates(std::uint64_t ticket, const lrclib::Query &query)
{
    const auto path = lrclib::search_path(query);
    std::vector<lrclib::Record> records;
    bool failed = true;
    if (!path.empty())
    {
        const auto result = fetch_(
            path, [&] { return stopping_.load() || candidates_wanted_.load() != ticket; });
        failed = !result.ok || result.status != 200 || !lrclib::parse_results(result.body, records);
    }
    auto list = failed ? std::vector<lrclib::Candidate>{} : lrclib::candidates(records, query);
    sys::log("[Afterglow] Other lyrics: %s", failed ? "lrclib.net busy" : std::to_string(list.size()).c_str());
    std::lock_guard<std::mutex> lock(mutex_);
    candidates_result_ticket_ = ticket;
    candidates_failed_ = failed;
    candidates_ = std::move(list);
    has_candidates_ = true;
}

bool lyrics_wanted(const Presentation &model)
{
    const auto &p = model.preferences;
    return p.display_mode == 3 || (p.lyrics_layer > 0 && p.display_mode < 2) ||
           model.page == "lyrics_timing";
}

void pump_lyrics(Presentation &model, LyricsLookup &lookup, OnlineLyrics &online,
                 const std::function<void()> &ask_again, std::int64_t now_ms)
{
    auto &c = model.companion;
    auto &track = c.track;
    LyricsSong song;
    if (c.connected && (!track.uri.empty() || !track.queue_item_id.empty()))
        song.key = c.queue_id + "\n" + track.queue_item_id + "\n" + track.uri;
    song.uri = track.uri;
    song.track = track.media_type == "track";
    song.query.title = track.title;
    song.query.artist = track.first_artist.empty() ? track.artist : track.first_artist;
    song.query.album = track.album;
    song.query.duration = track.duration;
    song.ma = static_cast<MaLyrics>(track.lyrics_reply);
    song.ma_request = track.lyrics_request;
    song.ma_plain = track.lyrics;
    song.ma_lrc = track.lrc_lyrics;
    if (model.preferences.online_lyrics)
        song.chosen = chosen_lyrics(model.preferences.songs, track.uri);

    if (model.lyrics_retry)
    {
        model.lyrics_retry = false;
        lookup.retry();
    }
    std::uint64_t ticket = 0;
    LyricsLookup::SearchOutcome outcome{};
    std::string plain, lrc;
    if (online.take_result(ticket, outcome, plain, lrc))
        lookup.search_done(ticket, outcome, plain, lrc);
    pump_song_lyrics(model, lookup, online);
    lookup.update(song, lyrics_wanted(model), model.preferences.online_lyrics, now_ms);
    if (lookup.take_ma_retry() && ask_again)
        ask_again();
    lrclib::Query query;
    if (lookup.take_search(query, ticket))
        online.search(ticket, query);
    if (lookup.take_choice_gone())
        clear_choice(model.preferences.songs, track.uri, epoch_ms());
    model.song_lyrics.source = lookup.source();
    model.lyrics = lookup.state();
    track.lyrics = lookup.plain();
    track.lrc_lyrics = lookup.lrc();
}
} // namespace hui::afterglow
