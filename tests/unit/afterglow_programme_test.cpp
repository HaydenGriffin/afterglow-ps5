// Afterglow - Tonight's programme: the session log, the merge and the page's lines.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/presentation.hpp"
#include "afterglow/programme.hpp"
#include <gtest/gtest.h>

namespace
{
using namespace hui::afterglow;
namespace wire = programme_wire;

// 16 October 2026 at hh:mm (local = utc here).
LocalTime at(int hour, int minute)
{
    const std::int64_t midnight = 1792108800;
    const std::int64_t t = midnight + hour * 3600 + minute * 60;
    return local_time_at(t, t);
}
CompanionSnapshot playing(int n, const char *artist = "Artist")
{
    CompanionSnapshot c;
    c.playing = true;
    c.queue_id = "q";
    c.track.uri = "lib://track/" + std::to_string(n);
    c.track.title = "Song " + std::to_string(n);
    c.track.artist = artist;
    c.track.media_type = "track";
    c.track.queue_item_id = "qi" + std::to_string(n);
    return c;
}
LoggedSong logged(const char *queue, int n, const char *artist, const LocalTime &when)
{
    LoggedSong s;
    s.queue_id = queue;
    s.uri = "lib://track/" + std::to_string(n);
    s.title = "Song " + std::to_string(n);
    s.artist = artist;
    s.local = when.local;
    return s;
}

TEST(AfterglowProgramme, SessionLogNotesEachSongOnceAsItStarts)
{
    SessionLog log;
    log.observe(playing(1), at(20, 0));
    log.observe(playing(1), at(20, 1));
    auto paused = playing(2);
    paused.playing = false;
    log.observe(paused, at(20, 4));
    log.observe(playing(2), at(20, 5));
    auto radio = playing(3);
    radio.track.media_type = "radio";
    log.observe(radio, at(20, 9));
    ASSERT_EQ(log.entries().size(), 2u);
    EXPECT_EQ(wire::clock_label(log.entries()[1].local), "20:05");
}

TEST(AfterglowProgramme, TonightUntilFiveOnTheEveningsDate)
{
    EXPECT_EQ(wire::headline(at(21, 0)).word, "Tonight");
    EXPECT_EQ(wire::headline(at(21, 0)).date, "16 October");
    EXPECT_EQ(wire::headline(at(14, 0)).word, "Today");
    const auto late = local_time_at(at(1, 30).utc + 86400, at(1, 30).local + 86400);
    EXPECT_EQ(wire::headline(late).word, "Tonight");
    EXPECT_EQ(wire::headline(late).date, "16 October");
}

TEST(AfterglowProgramme, HistoryIsAskedForThisQueuesLastFourHours)
{
    const auto args = wire::history_args("q", at(22, 0).utc);
    EXPECT_NE(args.find(R"("queue_id":"q")"), std::string::npos);
    EXPECT_NE(args.find(R"("media_types":["track"])"), std::string::npos);
    EXPECT_NE(args.find(R"("played_after_timestamp":)" + std::to_string(at(18, 0).utc)),
              std::string::npos);
}

TEST(AfterglowProgramme, HistoryOldestFirstThenTheLogsOwnWithArtistsAndTimes)
{
    const auto history = wire::decode_history(R"([{"uri":"lib://track/3","name":"Song 3"},)"
                                              R"({"uri":"lib://track/2","name":"Song 2"},)"
                                              R"({"uri":"lib://track/1","name":"Song 1"}])");
    ASSERT_EQ(history.size(), 3u);
    const std::vector<LoggedSong> log = {
        logged("q", 2, "Logged", at(20, 10)), logged("other", 9, "", at(20, 12)),
        logged("q", 4, "Now", at(20, 20)), logged("q", 0, "", at(12, 0))};
    CompanionTrack queued;
    queued.uri = "lib://track/1";
    queued.artist = "From the queue";
    const auto songs = wire::songs(&history, log, "q", {queued}, at(21, 0));
    ASSERT_EQ(songs.size(), 4u);
    EXPECT_EQ(songs[0].title, "Song 1");
    EXPECT_EQ(songs[0].artist, "From the queue");
    EXPECT_EQ(songs[0].local, -1);
    EXPECT_EQ(songs[1].artist, "Logged");
    EXPECT_EQ(wire::clock_label(songs[1].local), "20:10");
    EXPECT_EQ(songs[2].artist, "");
    EXPECT_EQ(songs[3].title, "Song 4");
    EXPECT_TRUE(wire::offered(songs));
    const auto alone = wire::songs(nullptr, log, "q", {}, at(21, 0));
    ASSERT_EQ(alone.size(), 2u) << "without history the log stands alone";
    EXPECT_FALSE(wire::offered(alone));
}

TEST(AfterglowProgramme, Lines)
{
    std::vector<ProgrammeSong> songs(3);
    songs[0].local = at(19, 40).local;
    songs[2].local = at(22, 15).local;
    EXPECT_EQ(wire::summary(songs), "3 songs · 19:40 to 22:15");
    EXPECT_EQ(wire::offer_detail(songs, "Kitchen"), "3 songs since 19:40");
    std::vector<ProgrammeSong> untimed(3);
    EXPECT_EQ(wire::summary(untimed), "3 songs");
    EXPECT_EQ(wire::offer_detail(untimed, "Kitchen"), "3 songs on Kitchen");
    std::vector<ProgrammeSong> many(30);
    const auto columns = wire::columns(many);
    EXPECT_EQ(columns[0].size(), 11u);
    EXPECT_EQ(columns[1].size(), 11u);
    EXPECT_EQ(columns[0][0].number, "01");
    EXPECT_TRUE(columns[1][10].more);
    EXPECT_EQ(columns[1][10].title, "and 9 more");
}

TEST(AfterglowProgramme, OfferedAfterHistoryArrivesAndPumpedThroughThePresentation)
{
    Presentation model;
    std::vector<ApiCall> sent;
    const auto send = [&](const ApiCall &call) { sent.push_back(call); };
    pump_playlists(model, {}, send);
    EXPECT_FALSE(model.programme.tile(at(21, 0)).has_value());
    model.programme.refresh("q", "Kitchen", at(21, 0));
    pump_playlists(model, {}, send);
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].command, "music/recently_played_items");
    ApiReply reply;
    reply.id = sent[0].id;
    reply.result =
        R"([{"uri":"lib://track/3","name":"Song 3"},{"uri":"lib://track/2","name":"Song 2"},)"
        R"({"uri":"lib://track/1","name":"Song 1"}])";
    pump_playlists(model, {reply}, send);
    const auto tile = model.programme.tile(at(21, 0));
    ASSERT_TRUE(tile.has_value());
    EXPECT_EQ(tile->word, "Tonight");
    EXPECT_EQ(tile->detail, "3 songs on Kitchen");
    EXPECT_FALSE(model.programme.is_open()) << "offered, never opened by itself";
    model.programme.open();
    pump_playlists(model, {}, send);
    EXPECT_EQ(sent.back().command, "player_queues/items");
}

TEST(AfterglowProgramme, ThePagesTwoActionsMoveActAndClose)
{
    Programme programme;
    programme.open();
    EXPECT_EQ(programme.focus, 0) << "opens on Keep it as a playlist";
    EXPECT_EQ(programme.press(ProgrammeKey::up), ProgrammeAct::refused);
    EXPECT_EQ(programme.press(ProgrammeKey::confirm), ProgrammeAct::keep);
    EXPECT_TRUE(programme.is_open()) << "keeping stays on the page to confirm";
    EXPECT_EQ(programme.press(ProgrammeKey::down), ProgrammeAct::moved);
    EXPECT_EQ(programme.focus, 1);
    EXPECT_EQ(programme.press(ProgrammeKey::down), ProgrammeAct::refused);
    EXPECT_EQ(programme.press(ProgrammeKey::confirm), ProgrammeAct::closed);
    EXPECT_FALSE(programme.is_open());
    programme.open();
    EXPECT_EQ(programme.focus, 0);
    EXPECT_EQ(programme.press(ProgrammeKey::back), ProgrammeAct::closed);
    EXPECT_FALSE(programme.is_open());
}

// The session's songs as Music Assistant and the log know them, pumped as main.cpp does.
struct Session
{
    Presentation model;
    std::vector<ApiCall> sent;
    void pump(std::vector<ApiReply> replies = {})
    {
        pump_playlists(model, std::move(replies), [&](const ApiCall &c) { sent.push_back(c); });
    }
    void reply(const char *result)
    {
        ApiReply r;
        r.id = sent.back().id;
        r.result = result;
        pump({r});
    }
};

TEST(AfterglowProgramme, KeepNamesThePlaylistForTheEveningAndSendsEverySongInOrder)
{
    Session s;
    s.model.programme.refresh("q", "Kitchen", at(1, 30));
    s.pump();
    s.reply(R"([{"uri":"lib://track/2","name":"Song 2"},{"uri":"lib://track/1","name":"Song 1"},)"
            R"({"uri":"lib://track/9","name":"Song 9"}])");
    ASSERT_TRUE(s.model.programme.keep(s.model.playlists, at(1, 30)));
    s.pump();
    EXPECT_EQ(s.sent.back().command, "music/playlists/library_items");
    EXPECT_EQ(s.sent.back().args, playlist_wire::name_check_args("Tonight · 15 Oct"))
        << "half past one is still the evening of the 15th";
    s.reply("[]");
    EXPECT_EQ(s.sent.back().command, "music/playlists/create_playlist");
    s.reply(R"({"item_id":"40"})");
    EXPECT_EQ(s.sent.back().args, R"({"db_playlist_id":"40","uris":["lib://track/9",)"
                                  R"("lib://track/1","lib://track/2"]})");
    s.reply(R"({"id":"t","status":"success","progress_text":"Added 3 item(s) to playlist"})");
    const auto page = s.model.programme.page(at(1, 30), s.model.playlists);
    EXPECT_EQ(page.keep_value, "Kept as “Tonight · 15 Oct”");
    EXPECT_TRUE(page.keep_tick);
    EXPECT_EQ(s.model.toast, "Kept as “Tonight · 15 Oct”");
}

TEST(AfterglowProgramme, ThePageSaysWhereHowManyAndWhen)
{
    Session s;
    s.model.programme.observe(playing(1, "Lightkeeper"), at(20, 4));
    s.model.programme.observe(playing(2, "Harbour Lamps"), at(20, 9));
    s.model.programme.observe(playing(3, "Lightkeeper"), at(20, 15));
    s.model.programme.refresh("q", "Kitchen", at(20, 16));
    s.pump();
    s.reply("[]");
    const auto page = s.model.programme.page(at(20, 16), s.model.playlists);
    EXPECT_EQ(page.where, "Kitchen");
    EXPECT_EQ(page.summary, "3 songs · 20:04 to 20:15");
    EXPECT_EQ(page.headline.word, "Tonight");
    EXPECT_EQ(page.headline.date, "16 October");
    ASSERT_EQ(page.columns[0].size(), 3u);
    EXPECT_EQ(page.columns[0][1].artist, "Harbour Lamps");
    EXPECT_EQ(page.columns[0][1].time, "20:09");
    EXPECT_TRUE(page.keep_value.empty());
    EXPECT_EQ(s.model.programme.tile(at(20, 16))->detail, "3 songs since 20:04");
}
} // namespace
