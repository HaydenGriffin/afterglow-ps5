// Afterglow - Lyrics loading order, fallback, retries and the online search worker.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The cases of LG Afterglow's test/lyrics-loading.test.mjs and lrclib.test.mjs,
// on the PS5's lookup. Nothing here reaches lrclib.net: every search is a fake.

#include "afterglow/lyrics_lookup.hpp"
#include "afterglow/online_lyrics.hpp"
#include "afterglow/presentation.hpp"
#include <atomic>
#include <gtest/gtest.h>
#include <unistd.h>

namespace
{
using namespace hui::afterglow;
using Outcome = LyricsLookup::SearchOutcome;

LyricsSong song(const char *id = "one")
{
    LyricsSong s;
    s.key = std::string("tv\n") + id;
    s.uri = std::string("library://track/") + id;
    s.track = true;
    s.query.title = "Song";
    s.query.artist = "Artist";
    s.query.album = "Album";
    s.query.duration = 350;
    return s;
}
void answer(LyricsSong &s, std::uint64_t request, const char *lrc = "", const char *plain = "")
{
    s.ma = MaLyrics::answered;
    s.ma_request = request;
    s.ma_lrc = lrc;
    s.ma_plain = plain;
}
// Runs the search the lookup asks for, answering with outcome.
int search(LyricsLookup &lookup, Outcome outcome, const char *lrc = "[00:01]Found")
{
    lrclib::Query query;
    std::uint64_t ticket = 0;
    if (!lookup.take_search(query, ticket))
        return 0;
    lookup.search_done(ticket, outcome, "", outcome == Outcome::found ? lrc : "");
    return 1;
}

TEST(AfterglowLyricsLookup, MusicAssistantLyricsAreUsedWithoutASearch)
{
    LyricsLookup lookup;
    auto s = song();
    lookup.update(s, true, true, 0);
    EXPECT_EQ(lookup.state().status, LyricsStatus::loading);
    answer(s, 1, "[00:01]From Music Assistant");
    lookup.update(s, true, true, 100);
    EXPECT_EQ(lookup.state().status, LyricsStatus::timed);
    EXPECT_EQ(lookup.lrc(), "[00:01]From Music Assistant");
    EXPECT_EQ(search(lookup, Outcome::found), 0);
}

TEST(AfterglowLyricsLookup, WhenMusicAssistantHasNoLyricsTheSearchFillsThemIn)
{
    LyricsLookup lookup;
    auto s = song();
    answer(s, 1);
    lookup.update(s, true, true, 0);
    lrclib::Query query;
    std::uint64_t ticket = 0;
    ASSERT_TRUE(lookup.take_search(query, ticket));
    EXPECT_EQ(query.title, "Song");
    EXPECT_EQ(query.artist, "Artist");
    EXPECT_EQ(query.album, "Album");
    EXPECT_DOUBLE_EQ(query.duration, 350);
    EXPECT_FALSE(lookup.take_search(query, ticket)) << "once";
    lookup.search_done(ticket, Outcome::found, "", "[00:18.88]Tryna put it all");
    EXPECT_EQ(lookup.state().status, LyricsStatus::timed);
    EXPECT_EQ(lookup.lrc(), "[00:18.88]Tryna put it all");
}

TEST(AfterglowLyricsLookup, OnlineLyricsOffAsksNothingAndOnAgainLooksUpOnRetry)
{
    LyricsLookup lookup;
    auto s = song();
    answer(s, 1);
    lookup.update(s, true, false, 0);
    EXPECT_EQ(search(lookup, Outcome::found), 0) << "no request leaves for lrclib.net";
    EXPECT_EQ(lookup.state().status, LyricsStatus::missing);
    lookup.retry();
    lookup.update(s, true, true, 10);
    EXPECT_TRUE(lookup.take_ma_retry()) << "a retry asks Music Assistant again too";
    answer(s, 2);
    lookup.update(s, true, true, 20);
    EXPECT_EQ(search(lookup, Outcome::found), 1) << "On again: the search runs";
    EXPECT_EQ(lookup.state().status, LyricsStatus::timed);
}

TEST(AfterglowLyricsLookup, TheSearchWaitsUntilLyricsAreOnScreen)
{
    LyricsLookup lookup;
    auto s = song();
    answer(s, 1);
    lookup.update(s, false, true, 0);
    EXPECT_EQ(search(lookup, Outcome::found), 0);
    EXPECT_EQ(lookup.state().status, LyricsStatus::loading);
    lookup.update(s, true, true, 60000);
    EXPECT_EQ(search(lookup, Outcome::found), 1);
}

TEST(AfterglowLyricsLookup, FallbackRunsWhenMusicAssistantRejectsTheRequest)
{
    LyricsLookup lookup;
    auto s = song();
    s.ma = MaLyrics::failed;
    s.ma_request = 1;
    lookup.update(s, true, true, 0);
    EXPECT_EQ(search(lookup, Outcome::found), 1);
    EXPECT_EQ(lookup.state().status, LyricsStatus::timed);
}

TEST(AfterglowLyricsLookup, ASlowMusicAssistantCannotBlockTheFallback)
{
    LyricsLookup lookup;
    auto s = song();
    lookup.update(s, true, true, 0);
    lookup.update(s, true, true, 4999);
    EXPECT_EQ(search(lookup, Outcome::found), 0);
    lookup.update(s, true, true, 5000);
    EXPECT_EQ(search(lookup, Outcome::found), 1);
    EXPECT_EQ(lookup.state().status, LyricsStatus::timed);
}

TEST(AfterglowLyricsLookup, LeavingASongDropsItsLateSearch)
{
    LyricsLookup lookup;
    auto s = song();
    answer(s, 1);
    lookup.update(s, true, true, 0);
    lrclib::Query query;
    std::uint64_t ticket = 0;
    ASSERT_TRUE(lookup.take_search(query, ticket));
    lookup.update(song("two"), true, true, 10);
    lookup.search_done(ticket, Outcome::found, "", "[00:01]Late");
    EXPECT_EQ(lookup.state().status, LyricsStatus::loading);
    EXPECT_EQ(lookup.lrc(), "");
    LyricsSong none;
    lookup.update(none, true, true, 20);
    EXPECT_EQ(lookup.state().status, LyricsStatus::idle);
}

TEST(AfterglowLyricsLookup, RetryInvalidatesARememberedMiss)
{
    LyricsLookup lookup;
    auto s = song();
    answer(s, 1);
    lookup.update(s, true, true, 0);
    search(lookup, Outcome::none);
    EXPECT_EQ(lookup.state().status, LyricsStatus::missing);
    // Back to the song later: the miss is remembered, nothing is asked.
    lookup.update(song("two"), true, true, 10);
    lookup.update(s, true, true, 20);
    EXPECT_EQ(lookup.state().status, LyricsStatus::missing);
    EXPECT_EQ(search(lookup, Outcome::found), 0);
    lookup.retry();
    lookup.update(s, true, true, 30);
    answer(s, 3);
    lookup.update(s, true, true, 40);
    EXPECT_EQ(search(lookup, Outcome::found), 1);
    EXPECT_EQ(lookup.state().status, LyricsStatus::timed);
}

TEST(AfterglowLyricsLookup, ProviderErrorsStayRetryableInsteadOfCachedAsMissing)
{
    LyricsLookup lookup;
    auto s = song();
    answer(s, 1);
    lookup.update(s, true, true, 0);
    search(lookup, Outcome::failed);
    EXPECT_EQ(lookup.state().status, LyricsStatus::error);
    lookup.update(song("two"), true, true, 10);
    lookup.update(s, true, true, 20);
    EXPECT_EQ(lookup.state().status, LyricsStatus::loading) << "an error is never remembered";
    lookup.retry();
    lookup.update(s, true, true, 30);
    answer(s, 2);
    lookup.update(s, true, true, 40);
    EXPECT_EQ(search(lookup, Outcome::failed), 1) << "asked again";
}

TEST(AfterglowLyricsLookup, TemporaryFailuresGetOneDelayedAutomaticRetry)
{
    LyricsLookup lookup;
    auto s = song();
    s.ma = MaLyrics::failed;
    s.ma_request = 1;
    lookup.update(s, true, false, 1000);
    EXPECT_EQ(lookup.state().status, LyricsStatus::error);
    EXPECT_EQ(lookup.state().retry_at_ms, 31000);
    lookup.update(s, true, false, 30999);
    EXPECT_FALSE(lookup.take_ma_retry());
    lookup.update(s, true, false, 31000);
    EXPECT_TRUE(lookup.take_ma_retry());
    EXPECT_EQ(lookup.state().status, LyricsStatus::loading);
    lookup.update(s, true, false, 31100);
    EXPECT_EQ(lookup.state().status, LyricsStatus::loading) << "the old failure no longer counts";
    s.ma_request = 2;
    lookup.update(s, true, false, 31200);
    EXPECT_EQ(lookup.state().status, LyricsStatus::error);
    EXPECT_EQ(lookup.state().retry_at_ms, 0) << "only once";
    lookup.update(s, true, false, 100000);
    EXPECT_FALSE(lookup.take_ma_retry());
}

TEST(AfterglowLyricsLookup, AMusicAssistantTimeoutThenAnLrclibMissStaysRetryable)
{
    LyricsLookup lookup;
    auto s = song();
    lookup.update(s, true, true, 1000);
    lookup.update(s, true, true, 6000);
    EXPECT_EQ(search(lookup, Outcome::none), 1);
    EXPECT_EQ(lookup.state().status, LyricsStatus::error) << "a timeout is not a clean miss";
    EXPECT_EQ(lookup.state().retry_at_ms, 36000);
    lookup.update(s, true, true, 36000);
    EXPECT_TRUE(lookup.take_ma_retry()) << "tried again after the cooldown";
}

TEST(AfterglowLyricsLookup, UntimedAndUnreadableLyricsAndRadio)
{
    LyricsLookup lookup;
    auto s = song();
    answer(s, 1, "no timestamps here", "Plain words");
    lookup.update(s, true, true, 0);
    EXPECT_EQ(lookup.state().status, LyricsStatus::plain);
    EXPECT_EQ(lookup.lrc(), "") << "an LRC with no timed line is not timed";
    EXPECT_EQ(lookup.plain(), "Plain words");
    auto radio = song("radio");
    radio.track = false;
    lookup.update(radio, true, true, 10);
    EXPECT_EQ(lookup.state().status, LyricsStatus::missing);
    EXPECT_EQ(search(lookup, Outcome::found), 0);
}

// ---- The worker and the pump --------------------------------------------

struct FakeFetch
{
    std::atomic<int> calls{0};
    std::string path;
    HttpsResult result;
    HttpsResult operator()(const std::string &asked, const std::function<bool()> &)
    {
        path = asked;
        ++calls;
        return result;
    }
};
bool wait_result(OnlineLyrics &online, std::uint64_t &ticket, Outcome &outcome, std::string &plain,
                 std::string &lrc)
{
    for (int i = 0; i < 200; ++i)
    {
        if (online.take_result(ticket, outcome, plain, lrc))
            return true;
        usleep(10000);
    }
    return false;
}

TEST(AfterglowOnlineLyrics, SearchesOnItsThreadAndPicksTheMatch)
{
    auto fake = std::make_shared<FakeFetch>();
    fake->result.ok = true;
    fake->result.status = 200;
    fake->result.body =
        R"j([{"trackName":"Song (Remastered)","artistName":"Artist","duration":349,"syncedLyrics":"[00:01]Hi","plainLyrics":"Hi"}])j";
    OnlineLyrics online([fake](const std::string &p, const std::function<bool()> &c)
                        { return (*fake)(p, c); });
    ASSERT_TRUE(online.start());
    lrclib::Query query;
    query.title = "Song";
    query.artist = "Artist";
    query.duration = 350;
    online.search(7, query);
    std::uint64_t ticket = 0;
    Outcome outcome{};
    std::string plain, lrc;
    ASSERT_TRUE(wait_result(online, ticket, outcome, plain, lrc));
    EXPECT_EQ(ticket, 7U);
    EXPECT_EQ(outcome, Outcome::found);
    EXPECT_EQ(lrc, "[00:01]Hi");
    EXPECT_EQ(fake->path, "/api/search?track_name=song&artist_name=Artist");

    fake->result.status = 429;
    online.search(8, query);
    ASSERT_TRUE(wait_result(online, ticket, outcome, plain, lrc));
    EXPECT_EQ(outcome, Outcome::failed) << "rate limited is retryable";
    fake->result.status = 200;
    fake->result.body = "<html>";
    online.search(9, query);
    ASSERT_TRUE(wait_result(online, ticket, outcome, plain, lrc));
    EXPECT_EQ(outcome, Outcome::failed);
    fake->result.ok = false;
    online.search(10, query);
    ASSERT_TRUE(wait_result(online, ticket, outcome, plain, lrc));
    EXPECT_EQ(outcome, Outcome::failed);
    fake->result.ok = true;
    fake->result.body = "[]";
    online.search(11, query);
    ASSERT_TRUE(wait_result(online, ticket, outcome, plain, lrc));
    EXPECT_EQ(outcome, Outcome::none);
    online.stop();
    online.join();
    EXPECT_EQ(fake->calls.load(), 5);
}

TEST(AfterglowOnlineLyrics, ThePumpFillsTheTrackAndOnlyAsksWhileLyricsAreWanted)
{
    auto fake = std::make_shared<FakeFetch>();
    fake->result.ok = true;
    fake->result.status = 200;
    fake->result.body =
        R"j([{"trackName":"Song","artistName":"Artist","duration":350,"syncedLyrics":"[00:01]Online","plainLyrics":""}])j";
    OnlineLyrics online([fake](const std::string &p, const std::function<bool()> &c)
                        { return (*fake)(p, c); });
    ASSERT_TRUE(online.start());
    LyricsLookup lookup;
    Presentation model;
    model.companion.connected = true;
    model.companion.queue_id = "tv";
    auto &track = model.companion.track;
    track.uri = "library://track/1";
    track.queue_item_id = "q1";
    track.media_type = "track";
    track.title = "Song";
    track.artist = "Artist, Guest";
    track.first_artist = "Artist";
    track.duration = 350;
    track.lyrics_reply = 1;
    track.lyrics_request = 4;
    int asked_again = 0;
    const auto again = [&] { ++asked_again; };
    model.preferences.display_mode = 1;
    model.preferences.lyrics_layer = 0;
    pump_lyrics(model, lookup, online, again, 0);
    EXPECT_FALSE(lyrics_wanted(model));
    usleep(100000);
    EXPECT_EQ(fake->calls.load(), 0) << "nothing on screen wants lyrics";
    model.preferences.display_mode = 3;
    for (int i = 0; i < 200 && model.lyrics.status != LyricsStatus::timed; ++i)
    {
        pump_lyrics(model, lookup, online, again, 100 + i);
        usleep(10000);
    }
    EXPECT_EQ(model.lyrics.status, LyricsStatus::timed);
    EXPECT_EQ(model.companion.track.lrc_lyrics, "[00:01]Online");
    EXPECT_EQ(fake->path, "/api/search?track_name=song&artist_name=Artist")
        << "the first artist, as LG";
    model.lyrics_retry = true;
    pump_lyrics(model, lookup, online, again, 1000);
    EXPECT_EQ(asked_again, 1);
    EXPECT_EQ(model.lyrics.status, LyricsStatus::loading);
    online.stop();
    online.join();
}
} // namespace
