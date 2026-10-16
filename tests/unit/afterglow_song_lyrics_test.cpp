// Afterglow - Kept timings, chosen lyrics and Go to a line (LG song-lyrics and lyrics-tools tests).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Nothing here reaches lrclib.net: every fetch is a fake.

#include "afterglow/lrclib.hpp"
#include "afterglow/lyrics_lookup.hpp"
#include "afterglow/online_lyrics.hpp"
#include "afterglow/panel.hpp"
#include "afterglow/preferences.hpp"
#include "afterglow/song_lyrics.hpp"
#include "afterglow/song_store.hpp"
#include "concept_fixture.hpp"
#include <atomic>
#include <memory>
#include <unistd.h>

namespace
{
using namespace hui::afterglow;
using hui::Action;
using hui::Direction;
using Page = Panel::Page;

TEST(AfterglowSongStore, FingerprintIsFnv1aOfTheUtf8WithoutCarriageReturnsOrOuterSpace)
{
    // The same values as LG test/song-lyrics.test.mjs.
    EXPECT_EQ(lyrics_fingerprint("a"), "e40c292c");
    EXPECT_EQ(lyrics_fingerprint("foobar"), "bf9cf968");
    EXPECT_EQ(lyrics_fingerprint("  foobar\n"), "bf9cf968");
    EXPECT_EQ(lyrics_fingerprint("[00:01]a\r\n[00:02]b"), lyrics_fingerprint("[00:01]a\n[00:02]b"));
    EXPECT_EQ(lyrics_fingerprint(""), "");
    EXPECT_EQ(lyrics_fingerprint(" \n"), "");
    EXPECT_EQ(lyrics_fingerprint("\xC5\x82"), "538f31a6");
    EXPECT_EQ(lyrics_fingerprint("\xE2\x82\xAC"), "298f832b");
    EXPECT_EQ(lyrics_fingerprint("\xF0\x9F\x8E\xB5"), "4cd7445d");
}

TEST(AfterglowSongStore, AKeptTimingAppliesOnlyToTheSameSongWithTheSameWords)
{
    SongStore songs;
    EXPECT_TRUE(keep_timing(songs, "library://track/1", "0000000a", 700, 1));
    EXPECT_EQ(kept_timing(songs, "library://track/1", "0000000a"), 700);
    EXPECT_FALSE(kept_timing(songs, "library://track/1", "0000000b")) << "other lyrics";
    EXPECT_FALSE(kept_timing(songs, "library://track/2", "0000000a"));
    EXPECT_TRUE(keep_timing(songs, "library://track/1", "0000000a", -300, 2));
    ASSERT_EQ(songs.size(), 1u) << "one entry per song";
    EXPECT_EQ(songs[0].at, 2);
    EXPECT_FALSE(keep_timing(songs, "library://track/1", "0000000a", -300, 3)) << "no change";
    EXPECT_TRUE(forget_timing(songs, "library://track/1", 4));
    EXPECT_TRUE(songs.empty()) << "nothing left to remember";
}

TEST(AfterglowSongStore, TheStoreKeepsTheHundredMostRecentlyChangedSongs)
{
    SongStore songs;
    for (int i = 0; i < 105; ++i)
        keep_timing(songs, "library://track/" + std::to_string(i), "0000000a", 100, 1000 + i);
    EXPECT_EQ(songs.size(), kSongLimit);
    EXPECT_FALSE(kept_timing(songs, "library://track/0", "0000000a")) << "the oldest went first";
    EXPECT_EQ(kept_timing(songs, "library://track/104", "0000000a"), 100);
    keep_timing(songs, "library://track/5", "0000000a", 200, 5000);
    keep_timing(songs, "library://track/new", "0000000a", 300, 5001);
    EXPECT_EQ(kept_timing(songs, "library://track/5", "0000000a"), 200);
    EXPECT_FALSE(kept_timing(songs, "library://track/6", "0000000a"));
}

TEST(AfterglowSongStore, ChoosingLyricsDropsATimingForOtherWordsKeepingOneKeepsTheChoice)
{
    SongStore songs;
    keep_timing(songs, "u", "0000000a", 500, 1);
    choose_lyrics(songs, "u", 42, "0000000b", 2);
    EXPECT_EQ(chosen_lyrics(songs, "u"), 42);
    EXPECT_FALSE(kept_timing(songs, "u", "0000000a"));
    keep_timing(songs, "u", "0000000b", 300, 3);
    ASSERT_EQ(songs.size(), 1u);
    EXPECT_EQ(songs[0].lrclib, 42);
    EXPECT_EQ(songs[0].ms, 300);
    forget_timing(songs, "u", 4);
    EXPECT_EQ(chosen_lyrics(songs, "u"), 42) << "forgetting the timing keeps the lyrics";
    EXPECT_TRUE(clear_choice(songs, "u", 5));
    EXPECT_TRUE(songs.empty());
}

TEST(AfterglowSongStore, NothingIsStoredWithoutAnIdentityOrValidValues)
{
    SongStore songs;
    EXPECT_FALSE(keep_timing(songs, "", "0000000a", 100, 1));
    EXPECT_FALSE(keep_timing(songs, std::string(301, 'x'), "0000000a", 100, 1));
    EXPECT_FALSE(keep_timing(songs, "u", "", 100, 1));
    EXPECT_FALSE(keep_timing(songs, "u", "0000000a", 10100, 1));
    EXPECT_FALSE(choose_lyrics(songs, "u", 0, "0000000a", 1));
    EXPECT_TRUE(songs.empty());
}

TEST(AfterglowSongStore, LinesAndJsonRoundTripAndRejectMalformedEntries)
{
    SongStore songs;
    keep_timing(songs, "library://track/1", "0000000a", -1200, 11);
    choose_lyrics(songs, "spotify://track/a,b \"q\"", 99, "0000000b", 12);
    SongEntry back;
    ASSERT_TRUE(decode_song_line(encode_song_line(songs[1]), back));
    EXPECT_EQ(back, songs[1]) << "an id may hold commas";
    EXPECT_FALSE(decode_song_line("1,-,-,0000000a,u", back)) << "nothing to remember";
    EXPECT_FALSE(decode_song_line("1,x,-,0000000a,u", back));
    EXPECT_FALSE(decode_song_line("1,100,-,NOTHEX!!,u", back));
    EXPECT_EQ(decode_songs_json(encode_songs_json(songs)), songs);
    const auto read = decode_songs_json(
        R"j([{"id":"a","sum":"0000000a","ms":100,"at":1},{"id":"b","sum":"nothex!!","ms":1},)j"
        R"j({"id":"c","sum":"0000000a","at":1},{"id":"dé","sum":"0000000a","lrclib":7,"at":2},)j"
        R"j({"id":"a","sum":"0000000b","ms":200,"at":3}])j");
    ASSERT_EQ(read.size(), 2u);
    EXPECT_EQ(read[0].id, "a");
    EXPECT_EQ(read[0].ms, 100);
    EXPECT_EQ(read[1].id, "d\xC3\xA9");
    EXPECT_EQ(read[1].lrclib, 7);
    EXPECT_TRUE(decode_songs_json("{not a list}").empty());
    EXPECT_TRUE(decode_songs_json("[{\"id\":").empty());
}

TEST(AfterglowSongStore, PreferencesKeepTheSongsLocallyAndOnTheAccount)
{
    Preferences p;
    p.brightness = 70;
    keep_timing(p.songs, "library://track/1", "0000000a", 300, 5);
    choose_lyrics(p.songs, "library://track/2", 77, "0000000b", 6);
    EXPECT_EQ(Preferences::decode(p.encode()).songs, p.songs);
    const auto synced = p.encode_synced();
    EXPECT_NE(synced.find("\"lyrics_songs\":[{\"id\":\"library://track/1\""), std::string::npos);
    const auto restored = Preferences{}.with_synced(synced);
    EXPECT_EQ(restored.songs, p.songs);
    EXPECT_EQ(restored.brightness, 70) << "keys after the list are still read";
    const auto reordered = Preferences{}.with_synced(
        "{\"lyrics_songs\":[{\"id\":\"u\",\"sum\":\"0000000a\",\"ms\":100,\"at\":1}],"
        "\"brightness\":40,\"later\":{\"x\":[1,2]},\"karaoke\":0}");
    EXPECT_EQ(reordered.brightness, 40);
    EXPECT_FALSE(reordered.karaoke) << "unknown objects are stepped over";
    EXPECT_EQ(kept_timing(reordered.songs, "u", "0000000a"), 100);
    EXPECT_EQ(p.with_synced("{\"brightness\":50}").songs, p.songs)
        << "a copy from before songs were kept leaves them alone";
}

TEST(AfterglowSongStore, TheKeepOfferAppearsOnceTheTimingChangesDuringASong)
{
    TimingOffer offer;
    offer.song("q:1", 0);
    EXPECT_FALSE(offer.pending(0, false, true, "u"));
    EXPECT_TRUE(offer.pending(300, false, true, "u"));
    EXPECT_EQ(offer.base(), 0);
    EXPECT_FALSE(offer.pending(300, true, true, "u")) << "a kept timing already applies";
    EXPECT_FALSE(offer.pending(300, false, false, "u")) << "untimed lyrics";
    EXPECT_FALSE(offer.pending(300, false, true, "")) << "no identity";
    offer.answer();
    EXPECT_FALSE(offer.pending(400, false, true, "u"));
    offer.song("q:1", 400);
    EXPECT_FALSE(offer.pending(500, false, true, "u")) << "the same song still";
    offer.song("q:2", 400);
    EXPECT_TRUE(offer.pending(500, false, true, "u"));
    EXPECT_EQ(offer.base(), 400);
}

TEST(AfterglowSongStore, GoingToALineSeeksToWhereTheTimingShowsIt)
{
    EXPECT_EQ(line_seek_position(35, 0, 240), 35);
    EXPECT_EQ(line_seek_position(35.6, 0, 240), 35);
    EXPECT_EQ(line_seek_position(35, 1500, 240), 36);
    EXPECT_EQ(line_seek_position(35, -1500, 240), 33);
    EXPECT_EQ(line_seek_position(0.5, -2000, 240), 0);
    EXPECT_EQ(line_seek_position(239.5, 2000, 240), 240);
    EXPECT_EQ(clock_label(65), "1:05");
}

lrclib::Record record(std::int64_t id, const char *track, double duration, const char *synced,
                      const char *plain = "", const char *album = "Coast")
{
    lrclib::Record r;
    r.id = id;
    r.track = track;
    r.artist = "Demo Band";
    r.album = album;
    r.duration = duration;
    r.synced = synced;
    r.plain = plain;
    return r;
}

TEST(AfterglowSongStore, CandidatesSameSongFirstThenNearestLengthTimedBeforePlain)
{
    lrclib::Query q;
    q.title = "Low Tide";
    q.artist = "Demo Band";
    q.duration = 200;
    std::vector<lrclib::Record> records = {
        record(1, "Something Else", 200, "[00:01]a"),
        record(2, "Low Tide", 320, "[00:01]live"),
        record(3, "Low Tide", 201, "", "plain"),
        record(4, "Low Tide", 201, "[00:01]timed"),
        record(5, "Low Tide", 200, "[00:01]timed", "", "Copy"),
        record(0, "Low Tide", 200, "[00:01]no id"),
        record(7, "Low Tide", 200, ""),
    };
    records.push_back(record(6, "Low Tide", 200, "[00:01]instrumental"));
    records.back().instrumental = true;
    const auto list = lrclib::candidates(records, q);
    std::vector<std::int64_t> ids;
    for (const auto &c : list)
        ids.push_back(c.id);
    EXPECT_EQ(ids, (std::vector<std::int64_t>{4, 3, 2, 1}));
    EXPECT_EQ(list[0].sum, lyrics_fingerprint("[00:01]timed"));
    EXPECT_TRUE(list[0].timed);
    EXPECT_FALSE(list[3].same);
    std::vector<lrclib::Record> many;
    for (int i = 0; i < 10; ++i)
        many.push_back(record(i + 1, "Low Tide", 200, ("[00:01]v" + std::to_string(i)).c_str()));
    EXPECT_EQ(lrclib::candidates(many, q).size(), lrclib::kCandidateLimit);
    EXPECT_EQ(candidate_detail(list[0], 200), "3:21 · Timed · Coast");
    EXPECT_EQ(candidate_detail(list[2], 200), "5:20 · 2:00 longer · Timed · Coast");
    lrclib::Candidate shorter;
    shorter.duration = 150;
    shorter.timed = true;
    EXPECT_EQ(candidate_detail(shorter, 200), "2:30 · 0:50 shorter · Timed");
}

TEST(AfterglowSongStore, ARecordIsFetchedByItsId)
{
    EXPECT_EQ(lrclib::get_path(42), "/api/get/42");
    EXPECT_EQ(lrclib::get_path(0), "");
    lrclib::Record r;
    ASSERT_TRUE(lrclib::parse_record(
        R"j({"id":42,"trackName":"Low Tide","artistName":"Demo Band","duration":200.0,"syncedLyrics":"[00:01]x","plainLyrics":null})j",
        r));
    EXPECT_EQ(r.id, 42);
    EXPECT_EQ(r.synced, "[00:01]x");
    EXPECT_FALSE(lrclib::parse_record("[{\"id\":1}]", r));
    EXPECT_FALSE(lrclib::parse_record("{\"id\":1", r));
}

// ---- The lookup with a chosen record ------------------------------------------

using Outcome = LyricsLookup::SearchOutcome;
LyricsSong song_with(std::int64_t chosen)
{
    LyricsSong s;
    s.key = "tv\none";
    s.uri = "library://track/1";
    s.track = true;
    s.query.title = "Low Tide";
    s.query.artist = "Demo Band";
    s.chosen = chosen;
    return s;
}

TEST(AfterglowSongLookup, AChosenRecordIsFetchedInsteadOfAskingMusicAssistant)
{
    LyricsLookup lookup;
    auto s = song_with(42);
    lookup.update(s, true, true, 0);
    lrclib::Query query;
    std::uint64_t ticket = 0;
    ASSERT_TRUE(lookup.take_search(query, ticket)) << "no wait for Music Assistant";
    EXPECT_EQ(query.record, 42);
    lookup.search_done(ticket, Outcome::found, "", "[00:01]Chosen");
    EXPECT_EQ(lookup.state().status, LyricsStatus::timed);
    EXPECT_EQ(lookup.source(), LyricsSource::chosen);
    EXPECT_FALSE(lookup.take_choice_gone());
}

TEST(AfterglowSongLookup, AGoneRecordIsForgottenAndTheUsualLookupRuns)
{
    LyricsLookup lookup;
    auto s = song_with(42);
    s.ma = MaLyrics::answered;
    s.ma_request = 1;
    s.ma_lrc = "[00:01]From Music Assistant";
    lookup.update(s, true, true, 0);
    lrclib::Query query;
    std::uint64_t ticket = 0;
    ASSERT_TRUE(lookup.take_search(query, ticket));
    lookup.search_done(ticket, Outcome::none, "", "");
    EXPECT_TRUE(lookup.take_choice_gone());
    lookup.update(s, true, true, 10);
    EXPECT_EQ(lookup.lrc(), "[00:01]From Music Assistant");
    EXPECT_EQ(lookup.source(), LyricsSource::music_assistant);
}

TEST(AfterglowSongLookup, AChosenRecordThatFailsIsARetryableError)
{
    LyricsLookup lookup;
    auto s = song_with(42);
    lookup.update(s, true, true, 0);
    lrclib::Query query;
    std::uint64_t ticket = 0;
    ASSERT_TRUE(lookup.take_search(query, ticket));
    lookup.search_done(ticket, Outcome::failed, "", "");
    EXPECT_EQ(lookup.state().status, LyricsStatus::error);
    EXPECT_NE(lookup.state().retry_at_ms, 0);
    EXPECT_FALSE(lookup.take_choice_gone());
    lookup.update(s, true, true, LyricsLookup::kRetryDelayMs);
    ASSERT_TRUE(lookup.take_search(query, ticket)) << "the automatic retry asks for it again";
    EXPECT_EQ(query.record, 42);
}

TEST(AfterglowSongLookup, PickedLyricsShowAtOnceAndOutrankALateAnswer)
{
    LyricsLookup lookup;
    auto s = song_with(0);
    lookup.update(s, true, true, 0);
    lookup.use("", "[00:01]Picked");
    EXPECT_EQ(lookup.state().status, LyricsStatus::timed);
    EXPECT_EQ(lookup.source(), LyricsSource::chosen);
    s.ma = MaLyrics::answered;
    s.ma_request = 1;
    s.ma_lrc = "[00:01]Late";
    lookup.update(s, true, true, 100);
    EXPECT_EQ(lookup.lrc(), "[00:01]Picked");
}

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

TEST(AfterglowSongLookup, TheWorkerFetchesByIdAndListsCandidates)
{
    auto fake = std::make_shared<FakeFetch>();
    fake->result.ok = true;
    fake->result.status = 404;
    OnlineLyrics online([fake](const std::string &p, const std::function<bool()> &c)
                        { return (*fake)(p, c); });
    ASSERT_TRUE(online.start());
    lrclib::Query query;
    query.title = "Low Tide";
    query.artist = "Demo Band";
    query.record = 42;
    online.search(3, query);
    std::uint64_t ticket = 0;
    Outcome outcome = Outcome::found;
    std::string plain, lrc;
    bool got = false;
    for (int i = 0; i < 200 && !got; ++i, usleep(10000))
        got = online.take_result(ticket, outcome, plain, lrc);
    ASSERT_TRUE(got);
    EXPECT_EQ(fake->path, "/api/get/42");
    EXPECT_EQ(outcome, Outcome::none) << "404: gone";
    fake->result.status = 503;
    query.record = 0;
    online.find_candidates(9, query);
    bool failed = false;
    std::vector<lrclib::Candidate> list;
    got = false;
    for (int i = 0; i < 200 && !got; ++i, usleep(10000))
        got = online.take_candidates(ticket, failed, list);
    ASSERT_TRUE(got);
    EXPECT_EQ(ticket, 9u);
    EXPECT_TRUE(failed) << "a busy lrclib.net";
    EXPECT_EQ(fake->path.rfind("/api/search?track_name=low%20tide", 0), 0u);
    online.stop();
    online.join();
}

// ---- The sheets -------------------------------------------------------------

class AfterglowSongSheets : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        presentation() = {};
        auto &s = presentation();
        s.companion_configured = true;
        s.companion.connected = true;
        s.companion.queue_id = "tv";
        s.companion.elapsed = 21;
        s.companion.track.title = "Low Tide";
        s.companion.track.uri = "library://track/1";
        s.companion.track.queue_item_id = "q1";
        s.companion.track.media_type = "track";
        s.companion.track.duration = 200;
        s.companion.track.lrc_lyrics =
            "[00:00]First line\n[00:10]\n[00:20]Second line\n[00:35]Third line";
        s.lyrics.status = LyricsStatus::timed;
        s.song_lyrics.source = LyricsSource::music_assistant;
        follow_song_lyrics(s);
    }
    void TearDown() override
    {
        presentation() = {};
    }
    void input(const hui::InputFrame &value)
    {
        follow_song_lyrics(presentation());
        panel_.tick(0.02f);
        panel_.update(value, 0.02f, feedback_, true);
    }
    const SheetRow *row(const std::string &title) const
    {
        for (const auto &r : panel_.rows())
            if (r.item.title == title)
                return &r;
        return nullptr;
    }
    // Long enough for the sheet's periodic rebuild.
    void refresh()
    {
        for (int i = 0; i < 40; ++i)
            input(idle());
    }
    // From the first row of a freshly shown page, down to `title` (headers are
    // skipped by the focus), then Cross.
    void pick(const std::string &title)
    {
        int steps = 0;
        bool found = false;
        for (const auto &r : panel_.rows())
        {
            if (r.item.title == title)
            {
                found = true;
                break;
            }
            steps += r.item.header ? 0 : 1;
        }
        ASSERT_TRUE(found) << title;
        for (int i = 0; i < steps; ++i)
            input(nav(Direction::down));
        input(press(Action::confirm));
    }
    Panel panel_;
};

TEST_F(AfterglowSongSheets, ChangingTheTimingOffersToKeepItForThisSong)
{
    auto &s = presentation();
    auto &p = s.preferences;
    p.lyrics_offset_ms = 200;
    s.song_lyrics.offer = {};
    follow_song_lyrics(s);
    panel_.open(Page::lyrics_timing);
    EXPECT_EQ(row("Keep"), nullptr) << "nothing changed during this song";
    input(nav(Direction::right));
    ASSERT_EQ(p.lyrics_offset_ms, 300);
    refresh();
    ASSERT_NE(row("Keep"), nullptr);
    EXPECT_EQ(row("Keep")->item.value, "Other songs: 0.2 s later");
    ASSERT_NE(row("Not now"), nullptr);
    EXPECT_NE(row("Reset lyrics timing"), nullptr);
    panel_.open(Page::lyrics_timing);
    pick("Keep");
    EXPECT_EQ(p.lyrics_offset_ms, 200) << "the other songs go back";
    EXPECT_EQ(kept_lyrics_timing(s), 300);
    EXPECT_EQ(lyrics_offset_ms(s), 300);
    EXPECT_EQ(s.toast, "Timing kept for this song");
    EXPECT_EQ(row("Keep"), nullptr);
    ASSERT_NE(row("Forget timing for this song"), nullptr);
    EXPECT_EQ(row("Forget timing for this song")->item.value, "Timing: kept for this song");
    EXPECT_EQ(row("Reset lyrics timing"), nullptr);
    EXPECT_EQ(row("Lyrics timing")->item.value, "0.3 s later");
    // Left/right now move the song's timing.
    panel_.open(Page::lyrics_timing);
    input(nav(Direction::right));
    EXPECT_EQ(kept_lyrics_timing(s), 400);
    EXPECT_EQ(p.lyrics_offset_ms, 200);
    // Other lyrics for the same song: the device timing.
    s.companion.track.lrc_lyrics = "[00:00]Other words";
    follow_song_lyrics(s);
    EXPECT_EQ(lyrics_offset_ms(s), 200);
    s.companion.track.lrc_lyrics =
        "[00:00]First line\n[00:10]\n[00:20]Second line\n[00:35]Third line";
    follow_song_lyrics(s);
    panel_.open(Page::lyrics_timing);
    pick("Forget timing for this song");
    EXPECT_FALSE(kept_lyrics_timing(s));
    EXPECT_TRUE(p.songs.empty());
}

TEST_F(AfterglowSongSheets, NotNowLeavesTheChangeAndAsksNoMoreForThisSong)
{
    auto &p = presentation().preferences;
    panel_.open(Page::lyrics_timing);
    input(nav(Direction::left));
    panel_.open(Page::lyrics_timing);
    pick("Not now");
    EXPECT_EQ(p.lyrics_offset_ms, -100);
    EXPECT_EQ(row("Keep"), nullptr);
    panel_.open(Page::lyrics_timing);
    input(nav(Direction::left));
    refresh();
    EXPECT_EQ(row("Keep"), nullptr) << "once per song";
    EXPECT_TRUE(p.songs.empty());
}

TEST_F(AfterglowSongSheets, ChooseOtherLyricsListsVersionsAndKeepsThePick)
{
    auto &s = presentation();
    panel_.open(Page::lyrics_timing);
    ASSERT_NE(row("Choose other lyrics"), nullptr);
    EXPECT_EQ(row("Choose other lyrics")->item.value, "From Music Assistant");
    pick("Choose other lyrics");
    ASSERT_EQ(panel_.page(), Page::lyrics_choice);
    EXPECT_TRUE(s.song_lyrics.candidates_wanted) << "the pump sends the search";
    EXPECT_EQ(lyrics_choice_note(s), "Searching lrclib.net…");
    // The pump's answer.
    lrclib::Candidate live;
    live.id = 12;
    live.title = "Low Tide (live)";
    live.artist = "Demo Band";
    live.album = "Live";
    live.duration = 290;
    live.timed = true;
    live.lrc = "[00:00]Live words";
    live.sum = lyrics_fingerprint(live.lrc);
    s.song_lyrics.candidates = {live};
    s.song_lyrics.candidates_state = CandidateState::ready;
    refresh();
    ASSERT_EQ(panel_.rows().size(), 2u);
    EXPECT_TRUE(panel_.rows()[0].chosen) << "Automatic";
    EXPECT_EQ(panel_.rows()[1].item.title, "Low Tide (live) · Demo Band");
    EXPECT_EQ(panel_.rows()[1].item.subtitle, "4:50 · 1:30 longer · Timed · Live");
    pick("Low Tide (live) · Demo Band");
    EXPECT_EQ(chosen_lyrics(s.preferences.songs, "library://track/1"), 12);
    ASSERT_TRUE(s.song_lyrics.pick.has_value()) << "the pump shows it at once";
    EXPECT_EQ(s.toast, "Lyrics chosen for this song");
    EXPECT_EQ(panel_.page(), Page::lyrics_timing) << "back on the Lyrics rows";
    EXPECT_EQ(row("Choose other lyrics")->item.value, "Chosen for this song");
    pick("Choose other lyrics");
    pick("Automatic");
    EXPECT_EQ(chosen_lyrics(s.preferences.songs, "library://track/1"), 0);
    EXPECT_TRUE(s.lyrics_retry);
    EXPECT_EQ(s.toast, "Lyrics back to automatic");
}

TEST_F(AfterglowSongSheets, ABusyLrclibOffersTryAgainAndOnlineOffHidesTheRow)
{
    auto &s = presentation();
    panel_.open(Page::lyrics_timing);
    pick("Choose other lyrics");
    s.song_lyrics.candidates_wanted = false;
    s.song_lyrics.candidates_state = CandidateState::error;
    refresh();
    EXPECT_EQ(lyrics_choice_note(s), "lrclib.net is busy. Try again shortly.");
    pick("Try again");
    EXPECT_TRUE(s.song_lyrics.candidates_wanted);
    EXPECT_EQ(panel_.page(), Page::lyrics_choice);
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::lyrics_timing);
    s.preferences.online_lyrics = false;
    panel_.open(Page::lyrics_timing);
    EXPECT_EQ(row("Choose other lyrics"), nullptr);
}

TEST_F(AfterglowSongSheets, BackFromChooseLyricsTwiceLandsOnSettingsLyricsRow)
{
    auto &s = presentation();
    s.preferences.lyrics_offset_ms = 300; // Reset lyrics timing shows, so the rows shift
    panel_.open(Page::settings);
    pick("Lyrics");
    ASSERT_EQ(panel_.page(), Page::lyrics_timing);
    pick("Choose other lyrics");
    ASSERT_EQ(panel_.page(), Page::lyrics_choice);
    EXPECT_TRUE(panel_.back());
    EXPECT_TRUE(panel_.back());
    ASSERT_EQ(panel_.page(), Page::settings);
    input(press(Action::confirm));
    EXPECT_EQ(panel_.page(), Page::lyrics_timing) << "the focus came back to Lyrics";
}

TEST_F(AfterglowSongSheets, GoToALineSeeksToThePickedLineAfterTheTiming)
{
    auto &s = presentation();
    s.preferences.lyrics_offset_ms = 1500;
    panel_.open(Page::details);
    ASSERT_NE(row("Go to a line"), nullptr);
    pick("Go to a line");
    ASSERT_EQ(panel_.page(), Page::lyrics_lines);
    ASSERT_EQ(panel_.rows().size(), 3u) << "the blank line is left out";
    EXPECT_EQ(panel_.rows()[0].item.title, "First line");
    EXPECT_EQ(panel_.rows()[1].item.value, "0:21");
    EXPECT_TRUE(panel_.rows()[0].chosen)
        << "21 s with lyrics 1.5 s later: a gap after the first line";
    EXPECT_EQ(lyrics_lines_note(s), "Low Tide · plays from the line you pick");
    input(nav(Direction::down));
    input(nav(Direction::down));
    input(press(Action::confirm));
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::seek);
    EXPECT_EQ(s.companion_action.value, 36);
    EXPECT_TRUE(panel_.take_leave()) << "back to the lyrics";
    s.companion.track.lrc_lyrics.clear();
    panel_.open(Page::details);
    EXPECT_EQ(row("Go to a line"), nullptr) << "only with timed lyrics";
}
} // namespace
