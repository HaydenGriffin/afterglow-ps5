// Afterglow - Go to rows, Continue listening, Speed and Chapters: parsing, requests and sheets.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/idle.hpp"
#include "afterglow/panel.hpp"
#include "afterglow/spoken_word.hpp"
#include "concept_fixture.hpp"

namespace
{
using hui::Action;
using hui::Direction;
using hui::afterglow::BrowseCategory;
using hui::afterglow::BrowseRequest;
using hui::afterglow::CompanionAction;
using hui::afterglow::CompanionActionKind;
using hui::afterglow::CompanionChapter;
using hui::afterglow::CompanionSnapshot;
using hui::afterglow::CompanionTrack;
using hui::afterglow::DetailsRowKind;
using hui::afterglow::IdleAct;
using hui::afterglow::IdleKey;
using hui::afterglow::IdleTileKind;
using hui::afterglow::LyricsStatus;
using hui::afterglow::presentation;
using Page = hui::afterglow::Panel::Page;
namespace wire = hui::afterglow::companion_wire;

CompanionTrack song()
{
    CompanionTrack t;
    t.title = "Slow Current";
    t.media_type = "track";
    t.uri = "library://track/7";
    t.duration = 240;
    t.album_link = {"12", "library", "Evening Light", "library://album/12"};
    t.artist_links = {{"3", "library", "Lightkeeper", "library://artist/3"},
                      {"4", "library", "Low Light Choir", "library://artist/4"}};
    return t;
}
CompanionTrack book()
{
    CompanionTrack t;
    t.title = "The Lighthouse Year";
    t.media_type = "audiobook";
    t.uri = "library://audiobook/1";
    t.duration = 7200;
    for (int i = 0; i < 4; ++i)
    {
        CompanionChapter chapter;
        chapter.name = i == 2 ? "" : "Part " + std::to_string(i + 1);
        chapter.start = i * 900;
        t.chapters.push_back(chapter);
    }
    return t;
}

TEST(AfterglowSpokenWord, SpeedLabelsAndSteps)
{
    using hui::afterglow::speed_label;
    using hui::afterglow::step_speed;
    EXPECT_EQ(speed_label(100), "1×");
    EXPECT_EQ(speed_label(75), "0.75×");
    EXPECT_EQ(speed_label(125), "1.25×");
    EXPECT_EQ(speed_label(150), "1.5×");
    EXPECT_EQ(speed_label(200), "2×");
    EXPECT_EQ(step_speed(100, 1, false), 125);
    EXPECT_EQ(step_speed(200, 1, false), 200) << "right stops at 2×";
    EXPECT_EQ(step_speed(75, -1, false), 75) << "left stops at 0.75×";
    EXPECT_EQ(step_speed(200, 1, true), 75) << "Cross wraps after 2×";
    EXPECT_EQ(step_speed(90, 1, false), 125) << "a speed set elsewhere steps from the nearest";
}

TEST(AfterglowSpokenWord, ChaptersAndClockLabels)
{
    using hui::afterglow::chapter_at;
    using hui::afterglow::clock_label;
    const auto chapters = book().chapters;
    EXPECT_EQ(chapter_at(chapters, 0), 0);
    EXPECT_EQ(chapter_at(chapters, 1799.6), 2) << "half a second early still counts";
    EXPECT_EQ(chapter_at({}, 10), -1);
    EXPECT_EQ(clock_label(245), "4:05");
    EXPECT_EQ(clock_label(3735), "1:02:15");
    const auto rows = hui::afterglow::chapter_rows(book(), 1000);
    ASSERT_EQ(rows.size(), 4U);
    EXPECT_EQ(rows[2].label, "Chapter 3") << "a chapter without a name is numbered";
    EXPECT_EQ(rows[1].value, "15:00");
    EXPECT_TRUE(rows[1].current);
    EXPECT_FALSE(rows[0].current);
}

TEST(AfterglowSpokenWord, DetailsRowsGoToOnlyWithIdsThenSpeedAndChapters)
{
    using hui::afterglow::details_rows;
    auto rows = details_rows(song(), 100, 0);
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].label, "Go to the album");
    EXPECT_EQ(rows[0].value, "Evening Light");
    EXPECT_EQ(rows[0].target.media_type, "album");
    EXPECT_EQ(rows[0].target.item_id, "12");
    EXPECT_EQ(rows[1].label, "Go to Lightkeeper");
    EXPECT_EQ(rows[2].target.media_type, "artist");
    auto bare = song();
    bare.album_link = {};
    bare.artist_links.clear();
    EXPECT_TRUE(details_rows(bare, 100, 0).empty()) << "never a name search";

    rows = details_rows(book(), 125, 1000);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].kind, DetailsRowKind::speed);
    EXPECT_EQ(rows[0].value, "1.25×");
    EXPECT_EQ(rows[1].value, "Chapter 2 of 4");
    auto stream = book();
    stream.duration = 0;
    EXPECT_TRUE(details_rows(stream, 100, 0).empty()) << "speed needs a duration";

    CompanionTrack episode;
    episode.media_type = "podcast_episode";
    episode.duration = 1500;
    episode.podcast_link = {"2", "library", "Quiet Hour", "library://podcast/2"};
    rows = details_rows(episode, 100, 0);
    ASSERT_EQ(rows.size(), 2U) << "one chapter is no list";
    EXPECT_EQ(rows[0].label, "Go to the podcast");
    EXPECT_EQ(rows[0].target.media_type, "podcast");

    CompanionTrack station;
    station.media_type = "radio";
    EXPECT_TRUE(details_rows(station, 100, 0).empty());
    EXPECT_FALSE(hui::afterglow::offers_radio(station));
    EXPECT_FALSE(hui::afterglow::offers_radio(book()));
    EXPECT_TRUE(hui::afterglow::offers_radio(song()));
    EXPECT_FALSE(hui::afterglow::offers_lyrics_retry(episode));
}

TEST(AfterglowSpokenWord, DecodesLinksChaptersAndAuthors)
{
    const auto track = wire::decode_track(
        R"({"queue_item_id":"q1","media_item":{"item_id":"7","provider":"library","media_type":"track",)"
        R"("uri":"library://track/7","name":"Slow Current","album":{"item_id":"12","provider":"library",)"
        R"("name":"Evening Light","uri":"library://album/12"},"artists":[{"item_id":"3","provider":"library",)"
        R"("name":"Lightkeeper"},{"name":"No id"},{"item_id":"4","provider":"x","name":"B"},)"
        R"({"item_id":"5","provider":"x","name":"C"},{"item_id":"6","provider":"x","name":"D"}]}})");
    EXPECT_EQ(track.album_link.item_id, "12");
    EXPECT_EQ(track.album_link.name, "Evening Light");
    ASSERT_EQ(track.artist_links.size(), 3U) << "up to three, only those with ids";
    EXPECT_EQ(track.artist_links[0].name, "Lightkeeper");
    EXPECT_EQ(track.artist_links[1].name, "B");

    const auto audiobook = wire::decode_track(
        R"({"item_id":"8","provider":"library","media_type":"audiobook","uri":"library://audiobook/8",)"
        R"("name":"A Year","duration":3600,"authors":[{"name":"A. Writer"}],"metadata":{"chapters":[)"
        R"({"position":2,"name":"Two","start":1002.17,"end":2000},{"position":1,"name":"One","start":0.0,"end":1002.17},)"
        R"({"position":3,"name":"Bad","start":null}]}})");
    EXPECT_EQ(audiobook.artist, "A. Writer");
    ASSERT_EQ(audiobook.chapters.size(), 2U);
    EXPECT_EQ(audiobook.chapters[0].name, "One") << "sorted by start";
    EXPECT_EQ(audiobook.chapters[1].start, 1002);

    const auto episode = wire::decode_track(
        R"({"item_id":"e1","provider":"library","media_type":"podcast_episode","uri":"library://podcast_episode/e1",)"
        R"("name":"Ep","podcast":{"item_id":"2","provider":"library","name":"Quiet Hour"}})");
    EXPECT_EQ(episode.podcast_link.item_id, "2");
    EXPECT_TRUE(episode.album_link.item_id.empty());
}

TEST(AfterglowSpokenWord, ContinueListeningRequestsAndSpeedCommand)
{
    BrowseRequest request;
    request.category = BrowseCategory::in_progress;
    const auto browse = wire::browse_request(request);
    EXPECT_EQ(browse.command, "music/in_progress_items");
    EXPECT_EQ(browse.args, R"({"limit":30})");
    EXPECT_FALSE(wire::browse_page(request).server_paged);

    const auto idle = wire::in_progress_request();
    EXPECT_EQ(idle.command, "music/in_progress_items");
    EXPECT_EQ(idle.args, R"({"limit":5})");
    const auto items = wire::decode_in_progress(
        R"([{"item_id":"1","provider":"library","name":"A Year","uri":"library://audiobook/1",)"
        R"("media_type":"audiobook","image":{"path":"http://x/c.jpg","provider":"y"}},{"name":"no uri"}])");
    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items[0].media_type, "audiobook");
    EXPECT_EQ(items[0].artwork, "http://x/c.jpg");

    CompanionSnapshot target;
    target.connected = true;
    target.queue_id = "tv";
    CompanionAction action;
    action.kind = CompanionActionKind::speed;
    action.value = 125;
    auto sent = wire::action_request(action, target);
    EXPECT_EQ(sent.command, "player_queues/set_playback_speed");
    EXPECT_EQ(sent.args, R"({"queue_id":"tv","speed":1.25})");
    action.value = 900;
    EXPECT_EQ(wire::action_request(action, target).args, R"({"queue_id":"tv","speed":3.00})");
    target.target_elsewhere = true;
    target.target_name = "Kitchen";
    EXPECT_TRUE(wire::action_request(action, target).command.empty());
}

TEST(AfterglowSpokenWord, IdlePosterCarriesOnAfterThePausedSong)
{
    CompanionTrack paused = song();
    std::vector<CompanionTrack> progress;
    for (int i = 0; i < 5; ++i)
    {
        CompanionTrack item = book();
        item.uri = "library://audiobook/" + std::to_string(i);
        item.media_type = i == 1 ? "podcast_episode" : "audiobook";
        progress.push_back(item);
    }
    std::vector<CompanionTrack> recent = {progress[0], song()};
    recent.push_back(song());
    recent.back().uri = "library://album/9";
    const auto tiles =
        hui::afterglow::idle_tiles(&paused, recent, hui::afterglow::RecentState::ready, progress);
    ASSERT_EQ(tiles.size(), 6U) << "paused, three in progress, one recent, Browse";
    EXPECT_EQ(tiles[0].kind, IdleTileKind::resume);
    EXPECT_EQ(tiles[1].kind, IdleTileKind::carry);
    EXPECT_EQ(tiles[3].kind, IdleTileKind::carry);
    EXPECT_EQ(tiles[4].kind, IdleTileKind::play) << "nothing shown twice";
    EXPECT_EQ(tiles[4].item.uri, "library://album/9");
    const auto text = hui::afterglow::idle_text(tiles[2], hui::afterglow::RecentState::ready, true);
    EXPECT_EQ(text.detail, "Episode · Carry on listening");
    EXPECT_EQ(text.hint, "Carry on");

    hui::afterglow::IdlePoster poster;
    hui::afterglow::IdlePoster::Inputs in;
    in.allowed = true;
    in.recent = hui::afterglow::RecentState::ready;
    in.tiles = static_cast<int>(tiles.size());
    poster.update(0.1f, in);
    poster.update(10, in);
    ASSERT_TRUE(poster.showing());
    IdleAct act = IdleAct::none;
    poster.press(IdleKey::right, tiles, act);
    poster.press(IdleKey::confirm, tiles, act);
    EXPECT_EQ(act, IdleAct::play) << "Cross plays it, which resumes";
}

class AfterglowDetails : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        presentation() = {};
        auto &s = presentation();
        s.companion_configured = true;
        s.companion.connected = true;
        s.companion.queue_id = "tv";
    }
    void TearDown() override
    {
        presentation() = {};
    }
    void input(const hui::InputFrame &value)
    {
        panel_.tick(0.02f);
        panel_.update(value, 0.02f, feedback_, true);
    }
    void cross()
    {
        input(press(Action::confirm));
    }
    std::vector<std::string> labels() const
    {
        std::vector<std::string> out;
        for (const auto &row : panel_.rows())
            out.push_back(row.item.title);
        return out;
    }
    const hui::afterglow::SheetRow *row(const std::string &label) const
    {
        for (const auto &r : panel_.rows())
            if (r.item.title == label)
                return &r;
        return nullptr;
    }
    hui::afterglow::Panel panel_;
};

TEST_F(AfterglowDetails, GoToTheAlbumOpensItsSongsAndBackReturnsToDetails)
{
    auto &s = presentation();
    s.companion.track = song();
    panel_.open(Page::details);
    EXPECT_EQ(labels(),
              (std::vector<std::string>{"Go to the album", "Go to Lightkeeper",
                                        "Go to Low Light Choir", "Add to music favourites",
                                        "Add to a playlist", "Start radio from this song"}));
    cross();
    ASSERT_EQ(panel_.page(), Page::collection);
    EXPECT_TRUE(panel_.covers_screen());
    EXPECT_TRUE(s.browse_pending);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::tracks);
    EXPECT_EQ(s.browse_request.media_type, "album");
    EXPECT_EQ(s.browse_request.item_id, "12");
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::details);
    EXPECT_FALSE(panel_.back()) << "Details was where it opened";

    input(nav(Direction::down));
    cross();
    ASSERT_EQ(panel_.page(), Page::artist);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::artist_albums);
    EXPECT_EQ(s.browse_request.item_id, "3");
}

TEST_F(AfterglowDetails, ABookOffersSpeedAndChaptersButNoRadio)
{
    auto &s = presentation();
    s.companion.track = book();
    s.companion.elapsed = 1000;
    s.companion.playback_speed = 100;
    panel_.open(Page::details);
    EXPECT_EQ(labels(), (std::vector<std::string>{"Speed", "Chapters", "Add to music favourites"}));
    cross();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::speed);
    EXPECT_EQ(s.companion_action.value, 125);
    s.companion_action_pending = false;
    s.companion.playback_speed = 75;
    input(nav(Direction::left));
    EXPECT_FALSE(s.companion_action_pending) << "left stops at 0.75×";
    input(nav(Direction::right));
    EXPECT_EQ(s.companion_action.value, 100);
    s.companion_action_pending = false;

    input(nav(Direction::down));
    cross();
    ASSERT_EQ(panel_.page(), Page::chapters);
    ASSERT_NE(row("Part 2"), nullptr);
    EXPECT_TRUE(row("Part 2")->chosen);
    ASSERT_NE(panel_.rows().size(), 0U);
    input(nav(Direction::down)); // from the playing chapter to the next
    cross();
    EXPECT_EQ(panel_.page(), Page::chapters) << "stays on the page";
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::seek);
    EXPECT_EQ(s.companion_action.value, 1800);
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::details);
}

TEST_F(AfterglowDetails, SpokenWordOffersNoRetryLyrics)
{
    auto &s = presentation();
    s.companion.track = book();
    s.lyrics.status = LyricsStatus::missing;
    panel_.open(Page::lyrics_timing);
    EXPECT_EQ(row("Retry lyrics"), nullptr);
    s.companion.track = song();
    panel_.open(Page::lyrics_timing);
    EXPECT_NE(row("Retry lyrics"), nullptr);
}

TEST_F(AfterglowDetails, ContinueListeningSitsAfterSearchAndSaysWhenEmpty)
{
    auto &s = presentation();
    panel_.open(Page::browse);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::recent)
        << "Browse opens on Recently played";
    input(nav(Direction::up));
    cross();
    EXPECT_EQ(s.browse_request.category, BrowseCategory::in_progress);
    s.browse_pending = false;
    s.companion.browse_items.clear();
    input(idle());
    ASSERT_FALSE(panel_.rows().empty());
    EXPECT_EQ(panel_.rows()[0].item.title, "Nothing to carry on with");
    EXPECT_EQ(panel_.rows()[0].item.subtitle, "Audiobooks and episodes you start show here");
}
} // namespace
