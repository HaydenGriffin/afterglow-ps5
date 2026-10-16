// Afterglow - Sheet behaviour: queue, players, settings, presets, browse, search and sync.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/panel.hpp"
#include "afterglow/style.hpp"
#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"

#include <cmath>

namespace
{
using hui::Action;
using hui::Direction;
using hui::afterglow::AccountCommand;
using hui::afterglow::AccountState;
using hui::afterglow::BrowseCategory;
using hui::afterglow::CompanionActionKind;
using hui::afterglow::CompanionPlayer;
using hui::afterglow::CompanionTrack;
using hui::afterglow::KeyboardPurpose;
using hui::afterglow::presentation;
using hui::afterglow::UiCommand;
using Page = hui::afterglow::Panel::Page;

class AfterglowPanel : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        presentation() = {};
    }
    void TearDown() override
    {
        presentation() = {};
    }
    // As the screen does: tick every frame, update while the sheet is open.
    void input(const hui::InputFrame &value)
    {
        panel_.tick(0.02f);
        panel_.update(value, 0.02f, feedback_, true);
    }
    void down(int times)
    {
        for (int i = 0; i < times; ++i)
            input(nav(Direction::down));
    }
    void cross()
    {
        input(press(Action::confirm));
    }
    void wait(float seconds)
    {
        const int frames = static_cast<int>(seconds / 0.02f + 0.5f);
        for (int i = 0; i < frames; ++i)
            input(idle());
    }
    // What main.cpp does once the system keyboard closes.
    static void type(KeyboardPurpose purpose, const char *text, bool accepted = true)
    {
        auto &s = presentation();
        s.keyboard_pending = false;
        s.keyboard_result.purpose = purpose;
        s.keyboard_result.accepted = accepted;
        s.keyboard_result.text = text;
        s.keyboard_result_ready = true;
    }
    static CompanionTrack item(const char *title, const char *id, const char *type)
    {
        CompanionTrack value = track(title, id);
        value.queue_item_id.clear();
        value.item_id = id;
        value.provider = "library";
        value.media_type = type;
        value.uri = std::string("library://") + type + "/" + id;
        return value;
    }
    // Music Assistant answering the last browse request.
    static void deliver(std::vector<CompanionTrack> items, bool more = false)
    {
        auto &s = presentation();
        s.browse_pending = false;
        s.companion.browse_items = std::move(items);
        s.companion.browse_has_more = more;
        ++s.companion.browse_revision;
    }
    static void live()
    {
        auto &s = presentation();
        s.companion_configured = true;
        s.companion.connected = true;
    }
    static CompanionTrack track(const char *title, const char *id)
    {
        CompanionTrack value;
        value.title = title;
        value.queue_item_id = id;
        value.uri = std::string("library://track/") + id;
        value.media_type = "track";
        return value;
    }
    hui::afterglow::Panel panel_;
};

TEST_F(AfterglowPanel, QueueOpensPlayersFromPlayingOnAndReturns)
{
    presentation().companion.connected = true;
    panel_.open(Page::queue);
    cross();
    EXPECT_EQ(panel_.page(), Page::players);
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::queue);
    EXPECT_FALSE(panel_.back()) << "the opened page is the root";
}

TEST_F(AfterglowPanel, EmptyQueueRowRefusesCrossAndPlaybackFollows)
{
    auto &s = presentation();
    s.companion.connected = true;
    panel_.open(Page::queue);
    // Playing on, then past the Up next heading to "Nothing queued next".
    down(1);
    cross();
    EXPECT_EQ(panel_.page(), Page::queue);
    EXPECT_FALSE(s.companion_action_pending);
    // Past Save as a playlist and the Playback heading to Shuffle.
    down(2);
    cross();
    EXPECT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::shuffle);
    EXPECT_TRUE(s.companion_action.enabled);
}

TEST_F(AfterglowPanel, VolumeAdjustsAndSleepTimerClears)
{
    auto &s = presentation();
    s.companion.connected = true;
    panel_.open(Page::queue);
    down(6); // nothing queued, save as a playlist, shuffle, repeat, crossfade, volume
    input(nav(Direction::right));
    EXPECT_EQ(s.command, UiCommand::volume_up);
    s.command = UiCommand::none;
    down(1);
    input(nav(Direction::left));
    input(nav(Direction::left));
    cross();
    EXPECT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::sleep);
    EXPECT_EQ(s.companion_action.value, 0);
    EXPECT_EQ(s.toast, "Sleep timer cleared");
}

TEST_F(AfterglowPanel, PlaybackRowsRefuseWhileOffline)
{
    auto &s = presentation();
    panel_.open(Page::queue);
    down(2);
    cross();
    EXPECT_FALSE(s.companion_action_pending);
}

TEST_F(AfterglowPanel, SelectsAutomaticAndSpecificSpeakerWithoutPlaybackCommand)
{
    auto &s = presentation();
    s.companion_configured = true;
    s.companion.players = {{"kitchen", "Kitchen"}, {"living", "Living room"}};
    panel_.open(Page::settings);
    cross();
    ASSERT_EQ(panel_.page(), Page::source);
    down(1);
    cross();
    EXPECT_TRUE(s.preferences.follow);
    EXPECT_TRUE(s.preferences.player.empty());
    down(1); // past the Speakers heading
    cross();
    EXPECT_EQ(s.preferences.player, "kitchen");
    down(1);
    cross();
    EXPECT_EQ(s.preferences.player, "living");
    for (int i = 0; i < 3; ++i)
        input(nav(Direction::up));
    cross();
    EXPECT_FALSE(s.preferences.follow);
    EXPECT_EQ(s.command, UiCommand::none);
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::settings);
    EXPECT_FALSE(panel_.back());
}

TEST_F(AfterglowPanel, FollowingRequiresConfiguredCompanion)
{
    panel_.open(Page::source);
    down(1);
    cross();
    EXPECT_FALSE(presentation().preferences.follow);
}

TEST_F(AfterglowPanel, DisplayTicksSetModeAndLyricsLayer)
{
    auto &p = presentation().preferences;
    panel_.open(Page::display);
    down(2);
    cross();
    EXPECT_EQ(p.display_mode, 2);
    down(3); // Lyrics, then past the heading to Off, then One line
    cross();
    EXPECT_EQ(p.lyrics_layer, 1);
    // Stage, the One line overlay's Position, Background and Size, then past
    // the Extras heading.
    down(5);
    cross();
    EXPECT_FALSE(p.poster_visuals);
    down(1);
    input(nav(Direction::right));
    EXPECT_EQ(p.track_info, 2);
    down(1);
    input(nav(Direction::left));
    EXPECT_EQ(p.visual_style, 2);
    down(1);
    cross();
    EXPECT_TRUE(p.reduce_motion);
}

TEST_F(AfterglowPanel, DisplayOverlayRowsActOnTheChosenOverlayOnly)
{
    auto &p = presentation().preferences;
    // The rows as the next frame shows them.
    const auto value_of = [this](const char *title) -> std::string
    {
        input(idle());
        for (const auto &row : panel_.rows())
            if (row.item.title == title)
                return row.item.value;
        return "(missing)";
    };
    panel_.open(Page::display);
    EXPECT_EQ(value_of("Position"), "(missing)") << "lyrics off: no overlay rows";
    down(5); // Visuals + track, Album, Lyrics, past the heading to Off, One line
    cross();
    ASSERT_EQ(p.lyrics_layer, 1);
    EXPECT_EQ(value_of("Position"), "Bottom");
    EXPECT_EQ(value_of("Background"), "Box");
    EXPECT_EQ(value_of("Size"), "Medium");
    down(2); // Stage, Position
    input(nav(Direction::right));
    EXPECT_EQ(p.line_look.position, 0);
    EXPECT_EQ(value_of("Position"), "Top");
    down(1);
    input(nav(Direction::right));
    EXPECT_EQ(value_of("Background"), "Shade");
    cross();
    EXPECT_EQ(value_of("Background"), "None") << "Cross steps on, like Track card";
    down(1);
    input(nav(Direction::left));
    EXPECT_EQ(value_of("Size"), "Small");
    EXPECT_EQ(p.line_look, (hui::afterglow::OverlayLook{0, 2, 0}));
    EXPECT_EQ(p.stage_look, hui::afterglow::kStageLookDefault) << "Stage keeps its own look";
    for (int i = 0; i < 3; ++i)
        input(nav(Direction::up));
    cross(); // Stage
    ASSERT_EQ(p.lyrics_layer, 2);
    EXPECT_EQ(value_of("Position"), "Middle");
    EXPECT_EQ(value_of("Background"), "Box");
    down(1);
    input(nav(Direction::left));
    EXPECT_EQ(p.stage_look.position, 0);
    EXPECT_EQ(p.line_look.position, 0) << "One line kept its Top";
    EXPECT_TRUE(p.encode().find("stage_position=0") != std::string::npos);
}

TEST_F(AfterglowPanel, AdjustsVisualsWithinBoundsAndRequests120Hz)
{
    auto &s = presentation();
    auto &p = s.preferences;
    panel_.open(Page::visuals);
    for (int i = 0; i < 15; ++i)
        input(nav(Direction::left));
    EXPECT_EQ(p.brightness, 10);
    down(1);
    input(nav(Direction::left));
    input(nav(Direction::left));
    EXPECT_EQ(p.text_brightness, 80);
    EXPECT_EQ(p.brightness, 10);
    down(1);
    input(nav(Direction::right));
    EXPECT_FALSE(p.tint) << "toggles want Cross, not left/right";
    cross();
    EXPECT_TRUE(p.tint);
    // Colour strength (tint is on now), cover art, night register, OLED, change
    // preset, order, lock, favourites, skip slow, minimum fps, level, beat
    // sensitivity, frame rate.
    down(13);
    input(nav(Direction::right));
    EXPECT_EQ(p.fps, 120);
    EXPECT_EQ(s.requested_refresh_hz, 120);
    EXPECT_EQ(s.accepted_refresh_hz, 60);
    EXPECT_FALSE(s.refresh_confirmed);
    EXPECT_EQ(s.command, UiCommand::none);
}

TEST_F(AfterglowPanel, LyricsTimingStepsTheOffset)
{
    auto &p = presentation().preferences;
    panel_.open(Page::settings);
    down(2);
    cross();
    ASSERT_EQ(panel_.page(), Page::lyrics_timing);
    input(nav(Direction::left));
    EXPECT_EQ(p.lyrics_offset_ms, -100) << "LG's 100 ms steps";
    for (int i = 0; i < 120; ++i)
        input(nav(Direction::right));
    EXPECT_EQ(p.lyrics_offset_ms, 10000) << "up to 10 s later";
    p.lyrics_offset_ms = 50; // saved in the old 50 ms steps
    input(nav(Direction::left));
    EXPECT_EQ(p.lyrics_offset_ms, 0) << "back onto the grid, through zero";
}

TEST_F(AfterglowPanel, LyricsPageGroupsTimingResetOnlineAndRetry)
{
    using hui::afterglow::LyricsStatus;
    auto &s = presentation();
    auto &p = s.preferences;
    panel_.open(Page::lyrics_timing);
    input(nav(Direction::right));
    ASSERT_EQ(p.lyrics_offset_ms, 100);
    down(1);
    cross();
    EXPECT_EQ(p.lyrics_offset_ms, 0) << "Reset lyrics timing, offered while not zero";
    // The reset row is gone; the focus now rests on Online lyrics.
    cross();
    EXPECT_FALSE(p.online_lyrics);
    EXPECT_FALSE(s.lyrics_retry) << "off asks nothing";
    s.lyrics.status = LyricsStatus::missing;
    cross();
    EXPECT_TRUE(p.online_lyrics);
    EXPECT_TRUE(s.lyrics_retry) << "back on: a song without lyrics is looked up again";
    s.lyrics_retry = false;
    down(1);
    cross();
    EXPECT_TRUE(s.lyrics_retry) << "Retry lyrics, offered when none were found";
    s.lyrics_retry = false;
    s.lyrics.status = LyricsStatus::timed;
    wait(0.1f);
    down(1);
    cross();
    EXPECT_FALSE(s.lyrics_retry) << "no Retry row once lyrics are there";
}

TEST_F(AfterglowPanel, ChoosesPresetTogglesFavouriteAndKeep)
{
    auto &s = presentation();
    s.presets = {"First", "Second"};
    panel_.open(Page::settings);
    down(3);
    cross();
    ASSERT_EQ(panel_.page(), Page::presets);
    down(1);
    input(press(Action::r3));
    EXPECT_TRUE(s.preferences.favorite("Second"));
    EXPECT_FALSE(s.preferences.favorite("First"));
    EXPECT_EQ(s.command, UiCommand::none);
    cross();
    EXPECT_EQ(s.command, UiCommand::preset_select);
    EXPECT_EQ(s.selected_preset, 1U);
    input(press(Action::west));
    ASSERT_TRUE(s.quality.ratings().count("Second"));
    EXPECT_TRUE(s.quality.ratings().at("Second").keep);
    EXPECT_FALSE(s.preferences.quality.empty());
}

TEST_F(AfterglowPanel, BrowseRequestsCategoryAndPlaysItemNow)
{
    auto &s = presentation();
    s.companion_configured = true;
    s.companion.connected = true;
    panel_.open(Page::browse);
    EXPECT_TRUE(panel_.covers_screen());
    EXPECT_TRUE(s.browse_pending) << "Browse opens already loading";
    EXPECT_EQ(s.browse_request.category, BrowseCategory::recent);
    s.browse_pending = false;
    down(1);
    cross();
    EXPECT_TRUE(s.browse_pending);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::favorite_playlists);
    s.browse_pending = false;
    s.companion.browse_items.push_back(track("A song", "1"));
    input(idle());
    cross();
    ASSERT_EQ(panel_.page(), Page::item_actions);
    EXPECT_FALSE(s.companion_action_pending);
    cross();
    EXPECT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::play_media);
    EXPECT_EQ(s.companion_action.text, "library://track/1");
    EXPECT_EQ(s.companion_action.option, "replace");
    EXPECT_NE(s.companion_action.id, 0u);
    EXPECT_TRUE(s.toast.empty()) << "nothing is claimed before Music Assistant answers";
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::browse);
    EXPECT_FALSE(panel_.back());
}

// Browse with one item open on its actions, as Music Assistant lists it.
void open_item_actions(hui::afterglow::Panel &panel, const CompanionTrack &value)
{
    auto &s = presentation();
    s.companion_configured = true;
    s.companion.connected = true;
    panel.open(Page::browse);
    s.browse_pending = false;
    s.companion.browse_items = {value};
    ++s.companion.browse_revision;
}

TEST_F(AfterglowPanel, PlayNowReturnsToNowPlayingOnceMusicAssistantAnswers)
{
    auto &s = presentation();
    open_item_actions(panel_, item("Blue Hour", "8", "album"));
    input(nav(Direction::right));
    cross();
    ASSERT_EQ(panel_.page(), Page::item_actions);
    cross(); // Play now
    ASSERT_TRUE(s.companion_action_pending);
    const auto id = s.companion_action.id;
    s.companion_action_pending = false; // main.cpp handed it on
    s.companion.action_busy = true;
    cross();
    EXPECT_FALSE(s.companion_action_pending) << "one play at a time";
    input(idle());
    EXPECT_FALSE(panel_.take_leave()) << "still waiting for the answer";
    s.companion.action_busy = false;
    s.companion.action_done = id;
    s.companion.action_done_ok = true;
    input(idle());
    EXPECT_EQ(s.toast, "Playing Blue Hour");
    EXPECT_TRUE(panel_.take_leave());
    EXPECT_FALSE(panel_.take_leave()) << "said once";
}

TEST_F(AfterglowPanel, StartRadioNamesTheRadioAndAlsoLeaves)
{
    auto &s = presentation();
    open_item_actions(panel_, item("Blue Hour", "8", "album"));
    input(nav(Direction::right));
    cross();
    down(3); // Play next, Add to the queue, Start radio from this
    cross();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::radio);
    s.companion_action_pending = false;
    s.companion.action_done = s.companion_action.id;
    s.companion.action_done_ok = true;
    input(idle());
    EXPECT_EQ(s.toast, "Playing Blue Hour radio");
    EXPECT_TRUE(panel_.take_leave());
}

TEST_F(AfterglowPanel, PlayNextConfirmsOnTheItemsOwnRowWithATick)
{
    auto &s = presentation();
    open_item_actions(panel_, item("Blue Hour", "8", "album"));
    input(nav(Direction::right));
    cross();
    down(1); // Play next
    cross();
    ASSERT_EQ(s.companion_action.option, "next");
    s.companion_action_pending = false;
    s.companion.action_done = s.companion_action.id;
    s.companion.action_done_ok = true;
    input(idle());
    EXPECT_FALSE(panel_.take_leave()) << "Play next stays in Browse";
    ASSERT_EQ(panel_.page(), Page::browse) << "back on the list";
    const auto row = [&]
    {
        for (const auto &r : panel_.rows())
            if (r.item.tag == 0)
                return r;
        return hui::afterglow::SheetRow{};
    };
    EXPECT_TRUE(row().chosen) << "the drawn tick";
    EXPECT_EQ(row().item.subtitle, "Plays next");
    EXPECT_EQ(panel_.rows().size(), 1u) << "the confirmation is the row itself";
    wait(3);
    EXPECT_FALSE(row().chosen);
    EXPECT_EQ(row().item.subtitle, "Album") << "the detail line returns";
    // Add to the queue says so in the same place.
    cross();
    down(2);
    cross();
    ASSERT_EQ(s.companion_action.option, "add");
    s.companion.action_done = s.companion_action.id;
    input(idle());
    EXPECT_EQ(row().item.subtitle, "Added to the queue");
}

TEST_F(AfterglowPanel, RefusedPlayIsNamedOnThePressedRowAndAnswersForAnotherPageAreIgnored)
{
    auto &s = presentation();
    open_item_actions(panel_, item("Blue Hour", "8", "album"));
    input(nav(Direction::right));
    cross();
    cross(); // Play now
    s.companion_action_pending = false;
    s.companion.action_done = s.companion_action.id;
    s.companion.action_done_ok = false;
    input(idle());
    EXPECT_FALSE(panel_.take_leave());
    ASSERT_EQ(panel_.page(), Page::item_actions);
    EXPECT_EQ(panel_.rows()[0].item.subtitle, "Couldn’t do that. Cross tries again");
    cross(); // tries again
    ASSERT_TRUE(s.companion_action_pending);
    s.companion_action_pending = false;
    input(idle());
    EXPECT_TRUE(panel_.rows()[0].item.subtitle.empty());
    EXPECT_TRUE(panel_.back()); // moved on before the answer
    s.companion.action_done = s.companion_action.id;
    s.companion.action_done_ok = true;
    input(idle());
    EXPECT_FALSE(panel_.take_leave()) << "the answer belongs to a page no longer shown";
}

TEST_F(AfterglowPanel, BrowseRowsNameTheKindMakerAndYearOrLength)
{
    CompanionTrack album = item("Blue Hour", "8", "album");
    album.artist = "Night Choir";
    album.year = "2019";
    EXPECT_EQ(hui::afterglow::browse_detail(album), "Album · Night Choir · 2019");
    CompanionTrack song = item("Low Tide", "9", "track");
    song.artist = "Night Choir";
    song.duration = 225;
    EXPECT_EQ(hui::afterglow::browse_detail(song), "Song · Night Choir · 3:45");
    song.duration = 0;
    EXPECT_EQ(hui::afterglow::browse_detail(song), "Song · Night Choir");
    EXPECT_EQ(hui::afterglow::browse_detail(item("Mix", "1", "playlist")), "Playlist");
    EXPECT_EQ(hui::afterglow::browse_detail(item("Someone", "2", "artist")), "Artist");
    EXPECT_EQ(hui::afterglow::browse_detail(item("FM", "3", "radio")), "Radio");
    CompanionTrack episode = item("Part 1", "4", "podcast_episode");
    episode.subtitle = "3 Oct · 42 min";
    EXPECT_EQ(hui::afterglow::browse_detail(episode), "Episode · 3 Oct · 42 min");
}

TEST_F(AfterglowPanel, SettingsReopensOnTheRowItWasLeftAt)
{
    panel_.open(Page::settings);
    down(3);
    panel_.open(Page::queue);
    panel_.open(Page::settings);
    cross();
    EXPECT_EQ(panel_.page(), Page::presets) << "the fourth row, Presets, kept the focus";
}

TEST_F(AfterglowPanel, BrowseWithoutMusicAssistantRequestsNothing)
{
    auto &s = presentation();
    panel_.open(Page::browse);
    cross();
    input(nav(Direction::right));
    cross();
    EXPECT_FALSE(s.browse_pending);
    EXPECT_FALSE(s.companion_action_pending);
}

TEST_F(AfterglowPanel, QueueEditsCarryStableItemIdAndRefuseDuplicates)
{
    auto &s = presentation();
    s.companion.connected = true;
    s.companion.upcoming.push_back(track("Next song", "queue-item-42"));
    panel_.open(Page::queue);
    down(1);
    cross();
    ASSERT_EQ(panel_.page(), Page::queue_actions);
    down(5);
    cross();
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::delete_item);
    EXPECT_EQ(s.companion_action.text, "queue-item-42");
    EXPECT_TRUE(s.companion_action_pending);
    EXPECT_EQ(panel_.page(), Page::queue);
    // The song is still listed while the removal is in flight: a second try is refused.
    s.companion_action.kind = CompanionActionKind::seek;
    cross();
    ASSERT_EQ(panel_.page(), Page::queue_actions);
    down(5);
    cross();
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::seek);
}

TEST_F(AfterglowPanel, DetailsFavouriteSendsTheSongAndConfirms)
{
    auto &s = presentation();
    s.companion.connected = true;
    s.companion.track = track("Song", "7");
    panel_.open(Page::details);
    cross();
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::favorite);
    EXPECT_TRUE(s.companion_action.enabled);
    EXPECT_EQ(s.toast, "Added to music favourites");
}

TEST_F(AfterglowPanel, DrawsEveryPageInsideTheSafeArea)
{
    auto &s = presentation();
    s.companion_configured = true;
    s.companion.connected = true;
    s.presets = {"First", "Second"};
    s.companion.upcoming.push_back(track("Next song", "1"));
    s.companion.track = track("Song", "2");
    s.companion.track.lrc_lyrics = "[00:01.00]First line\n[00:04.00]Second line";
    s.thumbnail = [](const std::string &) { return 0U; };
    s.dev_status = "quit request honoured";
    s.companion.static_delay_ms = 120;
    s.companion.track.quality = "Lossless";
    s.companion.track.quality_badge = "Lossless · FLAC 44.1 kHz / 16-bit";
    s.account.source = AccountState::Source::config;
    s.account.host = "music.local";
    for (Page page : {Page::browse, Page::queue, Page::details, Page::display, Page::settings,
                      Page::source, Page::visuals, Page::presets, Page::lyrics_timing, Page::status,
                      Page::search, Page::players, Page::sync, Page::account, Page::home})
    {
        panel_.open(page);
        s.browse_pending = false;
        s.companion.browse_items = {track("Item", "3")};
        input(idle());
        hui::gfx::DrawList list;
        hui::ui::Canvas canvas{list, fonts_, 0, 0};
        panel_.draw(canvas);
        EXPECT_FALSE(list.instances().empty());
        for (const auto &shape : list.instances())
        {
            for (float value : shape.rect)
                ASSERT_TRUE(std::isfinite(value));
            // Anything that starts in the sheet's column ends inside the safe edge.
            if (shape.rect[0] >= hui::afterglow::style::kColumn)
                EXPECT_LE(shape.rect[0] + shape.rect[2], hui::afterglow::style::kSafeRight + 0.5f)
                    << static_cast<int>(page);
        }
    }
}
TEST_F(AfterglowPanel, SearchAsksTheKeyboardAndSendsTheQuery)
{
    auto &s = presentation();
    live();
    panel_.open(Page::browse);
    s.browse_pending = false;
    input(nav(Direction::up)); // past Continue listening
    input(nav(Direction::up)); // to Search
    cross();
    ASSERT_EQ(panel_.page(), Page::search);
    EXPECT_TRUE(panel_.covers_screen());
    EXPECT_TRUE(s.keyboard_pending);
    EXPECT_EQ(s.keyboard.purpose, KeyboardPurpose::search);
    EXPECT_FALSE(s.keyboard.secret);
    EXPECT_FALSE(s.browse_pending) << "nothing to search for yet";
    type(KeyboardPurpose::search, "  slow current ");
    input(idle());
    EXPECT_FALSE(s.keyboard_result_ready) << "the answer is taken once";
    ASSERT_TRUE(s.browse_pending);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::search);
    EXPECT_EQ(s.browse_request.query, "slow current");
    EXPECT_EQ(s.browse_request.offset, 0);
}

TEST_F(AfterglowPanel, SearchCancelledOrForAnotherFieldSendsNothing)
{
    auto &s = presentation();
    live();
    panel_.open(Page::browse);
    s.browse_pending = false;
    input(nav(Direction::up)); // past Continue listening
    input(nav(Direction::up));
    cross();
    type(KeyboardPurpose::sign_in_host, "music.local");
    input(idle());
    EXPECT_TRUE(s.keyboard_result_ready) << "not the search's answer";
    EXPECT_FALSE(s.browse_pending);
    type(KeyboardPurpose::search, "light", false);
    input(idle());
    EXPECT_FALSE(s.browse_pending);
    EXPECT_EQ(panel_.page(), Page::search);
}

TEST_F(AfterglowPanel, SearchResultsGroupByTypeAndArtistsOpenTheirAlbums)
{
    auto &s = presentation();
    live();
    panel_.open(Page::browse);
    input(nav(Direction::up)); // past Continue listening
    input(nav(Direction::up));
    cross();
    type(KeyboardPurpose::search, "light");
    input(idle());
    // Songs arrive first, but Artists lead the results.
    deliver({item("A song", "1", "track"), item("The Band", "2", "artist"),
             item("A record", "3", "album")});
    input(idle());
    cross();
    ASSERT_EQ(panel_.page(), Page::item_actions);
    s.companion_action_pending = false;
    cross(); // Play now
    EXPECT_EQ(s.companion_action.text, "library://artist/2");
    s.companion_action_pending = false;
    down(2); // Start radio, Show albums
    cross();
    ASSERT_EQ(panel_.page(), Page::artist);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::artist_albums);
    EXPECT_EQ(s.browse_request.item_id, "2");
    EXPECT_EQ(s.browse_request.provider, "library");
    deliver({item("First album", "4", "album")});
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::item_actions);
    down(3);
    cross(); // Show songs
    ASSERT_EQ(panel_.page(), Page::artist);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::artist_tracks);
    deliver({item("Their song", "5", "track")});
    EXPECT_TRUE(panel_.back());
    EXPECT_TRUE(panel_.back());
    ASSERT_EQ(panel_.page(), Page::search);
    EXPECT_TRUE(s.browse_pending) << "the artist's songs replaced the results";
    EXPECT_EQ(s.browse_request.category, BrowseCategory::search);
    EXPECT_EQ(s.browse_request.query, "light");
    s.browse_pending = false;
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::browse);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::recent);
    EXPECT_FALSE(panel_.back());
}

TEST_F(AfterglowPanel, SearchAgainAndSquareReopenTheKeyboardWithTheQuery)
{
    auto &s = presentation();
    live();
    panel_.open(Page::browse);
    input(nav(Direction::up)); // past Continue listening
    input(nav(Direction::up));
    cross();
    type(KeyboardPurpose::search, "light");
    input(idle());
    deliver({item("A song", "1", "track")});
    input(idle());
    input(press(Action::west));
    EXPECT_TRUE(s.keyboard_pending);
    EXPECT_EQ(s.keyboard.initial, "light");
    s.keyboard_pending = false;
    input(nav(Direction::up)); // the Search again row
    cross();
    EXPECT_TRUE(s.keyboard_pending);
    EXPECT_EQ(s.keyboard.purpose, KeyboardPurpose::search);
}

TEST_F(AfterglowPanel, MoreAppendsTheNextPageAndKeepsThePlace)
{
    auto &s = presentation();
    live();
    panel_.open(Page::browse);
    deliver({item("One", "1", "album"), item("Two", "2", "album")}, true);
    input(idle());
    input(nav(Direction::right));
    down(2); // past both albums to More
    cross();
    ASSERT_TRUE(s.browse_pending);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::recent);
    EXPECT_EQ(s.browse_request.offset, 2);
    s.browse_pending = false;
    s.companion.browse_loading = true; // the backend keeps the rows while it appends
    input(nav(Direction::up));
    cross();
    ASSERT_EQ(panel_.page(), Page::item_actions) << "the list stays while more loads";
    EXPECT_TRUE(panel_.back());
    input(nav(Direction::down)); // back on the More row, now "Loading more…"
    cross();
    EXPECT_FALSE(s.browse_pending) << "Loading more refuses Cross";
    s.companion.browse_loading = false;
    deliver({item("One", "1", "album"), item("Two", "2", "album"), item("Three", "3", "album")});
    input(idle());
    cross();
    ASSERT_EQ(panel_.page(), Page::item_actions);
    cross();
    EXPECT_EQ(s.companion_action.text, "library://album/3") << "the focus lands on the new item";
}

TEST_F(AfterglowPanel, PodcastsListAndOpenEpisodes)
{
    auto &s = presentation();
    live();
    panel_.open(Page::browse);
    down(8); // Recently played to Podcasts
    cross();
    EXPECT_EQ(s.browse_request.category, BrowseCategory::podcasts);
    deliver({item("A show", "9", "podcast")});
    input(idle());
    cross();
    ASSERT_EQ(panel_.page(), Page::item_actions);
    down(3); // Play next, Add to the queue, Show episodes: no radio for podcasts
    cross();
    EXPECT_EQ(panel_.page(), Page::collection);
    EXPECT_EQ(s.browse_request.category, BrowseCategory::podcast_episodes);
    EXPECT_EQ(s.browse_request.item_id, "9");
    s.browse_pending = false;
    input(nav(Direction::left));
    down(1);
    cross();
    EXPECT_EQ(s.browse_request.category, BrowseCategory::audiobooks);
}

TEST_F(AfterglowPanel, QueuePagesThroughWindowsOfFifty)
{
    auto &s = presentation();
    s.companion.connected = true;
    s.companion.queue_offset = 50;
    s.companion.queue_total = 120;
    for (int i = 0; i < 50; ++i)
        s.companion.upcoming.push_back(track("Song", std::to_string(i).c_str()));
    panel_.open(Page::queue);
    down(1);
    cross();
    EXPECT_EQ(s.queue_offset_request, hui::afterglow::kQueueOffsetDefault)
        << "Earlier songs back at the start return to the default window";
    s.queue_offset_request = -1;
    down(51);
    cross();
    EXPECT_EQ(s.queue_offset_request, 100) << "Later songs";
    EXPECT_EQ(panel_.page(), Page::queue);
}

TEST_F(AfterglowPanel, PlayNextCountsTheWindowOffset)
{
    auto &s = presentation();
    s.companion.connected = true;
    s.companion.queue_offset = 50;
    s.companion.queue_total = 52;
    s.companion.upcoming = {track("A", "a"), track("B", "b")};
    panel_.open(Page::queue);
    down(3); // Earlier songs, A, B
    cross();
    ASSERT_EQ(panel_.page(), Page::queue_actions);
    down(1);
    cross();
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::move_item);
    EXPECT_EQ(s.companion_action.value, -51);
}

TEST_F(AfterglowPanel, QueueOffsetsAreQueueIndexesAfterThePlayingSong)
{
    auto &s = presentation();
    s.companion.connected = true;
    // Song 10 plays; the window starts at queue index 61, the 51st song up next.
    s.companion.queue_index = 10;
    s.companion.queue_offset = 61;
    s.companion.queue_total = 113;
    s.companion.upcoming = {track("A", "a"), track("B", "b")};
    panel_.open(Page::queue);
    down(1);
    cross();
    EXPECT_EQ(s.queue_offset_request, hui::afterglow::kQueueOffsetDefault)
        << "Earlier songs stop at the song after the playing one";
    s.queue_offset_request = -1;
    down(2); // A, B
    cross();
    ASSERT_EQ(panel_.page(), Page::queue_actions);
    down(1);
    cross();
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::move_item);
    EXPECT_EQ(s.companion_action.value, -51);
}

TEST_F(AfterglowPanel, LaterSongsCountFromTheSongAfterThePlayingOne)
{
    hui::afterglow::CompanionSnapshot c;
    // Song 10 plays in a queue of 113: 102 songs up next, 50..51 shown.
    c.queue_index = 10;
    c.queue_offset = 61;
    c.queue_total = 113;
    c.upcoming.resize(2);
    EXPECT_EQ(hui::afterglow::later_songs(c), 50);
    c.queue_total = 63;
    EXPECT_EQ(hui::afterglow::later_songs(c), 0) << "the window reaches the end";
    c.queue_total = 40; // a stale total never goes negative
    EXPECT_EQ(hui::afterglow::later_songs(c), 0);
    c.queue_index = -1;
    c.queue_offset = 0;
    c.queue_total = 120;
    c.upcoming.resize(50);
    EXPECT_EQ(hui::afterglow::later_songs(c), 70);
}

TEST_F(AfterglowPanel, LaterSongsHidesWhenNothingIsLeft)
{
    auto &s = presentation();
    s.companion.connected = true;
    s.companion.queue_index = 10;
    s.companion.queue_offset = 61;
    s.companion.queue_total = 63; // absolute counting would claim 11 more
    s.companion.upcoming = {track("A", "a"), track("B", "b")};
    panel_.open(Page::queue);
    down(5); // Earlier songs, A, B, Save as a playlist, then Shuffle: no Later songs row
    cross();
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::shuffle);
    EXPECT_EQ(s.queue_offset_request, -1);
}

TEST_F(AfterglowPanel, CompensationSavesEvenWhenTheSheetClosesFirst)
{
    auto &s = presentation();
    live();
    s.companion.static_delay_ms = 100;
    s.companion.own_player_id = "ps5";
    panel_.open(Page::sync);
    input(nav(Direction::right));
    // The sheet closes at once: only the screen's per-frame tick runs.
    for (int i = 0; i < 40; ++i)
        panel_.tick(0.02f);
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::static_delay);
    EXPECT_EQ(s.companion_action.value, 110);
}

TEST_F(AfterglowPanel, KeepPlayingTogglesAutoplay)
{
    auto &s = presentation();
    s.companion.connected = true;
    panel_.open(Page::queue);
    // Nothing queued, save as a playlist, shuffle, repeat, crossfade, volume, sleep timer,
    // keep playing.
    down(8);
    cross();
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::autoplay);
    EXPECT_TRUE(s.companion_action.enabled);
}

TEST_F(AfterglowPanel, PlayersChooseASpeakerAndToggleItsGroup)
{
    auto &s = presentation();
    live();
    CompanionPlayer room{"room", "Listening room"};
    room.can_group_with = {"kitchen", "garden"};
    room.can_set_members = true;
    CompanionPlayer kitchen{"kitchen", "Kitchen"};
    CompanionPlayer garden{"garden", "Garden"};
    garden.available = false;
    s.companion.players = {room, kitchen, garden};
    // Speakers together belongs to the command target, which leads the group.
    s.companion.queue_id = "room";
    panel_.open(Page::queue);
    cross();
    ASSERT_EQ(panel_.page(), Page::players);
    down(2); // Active speaker, then past the heading to Listening room
    cross();
    EXPECT_TRUE(s.preferences.follow);
    EXPECT_EQ(s.preferences.player, "room");
    EXPECT_FALSE(s.companion_action_pending) << "choosing a speaker sends nothing";
    down(1); // Kitchen
    down(1); // Garden is unavailable: the focus may rest on it but Cross refuses
    cross();
    EXPECT_EQ(s.preferences.player, "room");
    down(2); // past the Speakers together heading and the leader to Kitchen
    cross();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::group_add);
    EXPECT_EQ(s.companion_action.option, "room");
    EXPECT_EQ(s.companion_action.text, "kitchen");
    s.companion_action_pending = false;
    s.companion.players[0].group_members = {"room", "kitchen"};
    input(idle());
    cross();
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::group_remove);
    s.companion_action_pending = false;
    down(1); // Garden: unavailable, so it refuses
    cross();
    EXPECT_FALSE(s.companion_action_pending);
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::queue);
}

TEST_F(AfterglowPanel, PlayingOnASilentSpeakerMovesTheMusicThere)
{
    auto &s = presentation();
    live();
    CompanionPlayer room{"room", "Listening room"};
    CompanionPlayer kitchen{"kitchen", "Kitchen"};
    kitchen.takes_music = true; // speakers.hpp moves_music, worked out by the companion
    s.companion.players = {room, kitchen};
    s.companion.queue_id = "room";
    s.preferences.follow = true;
    s.preferences.player = "room";
    panel_.open(Page::queue);
    cross();
    ASSERT_EQ(panel_.page(), Page::players);
    down(3); // Active speaker, then past the heading to Listening room, Kitchen
    cross();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::transfer);
    EXPECT_EQ(s.companion_action.text, "kitchen");
    EXPECT_FALSE(s.companion_action.enabled) << "the queue keeps its own playing state";
    EXPECT_EQ(s.toast, "Moved the music to Kitchen");
    EXPECT_TRUE(s.preferences.follow);
    EXPECT_EQ(s.preferences.player, "kitchen") << "and Afterglow follows it there";

    // A speaker with music of its own is just followed.
    s.companion_action_pending = false;
    input(nav(Direction::up));
    cross();
    EXPECT_FALSE(s.companion_action_pending);
    EXPECT_EQ(s.preferences.player, "room");

    // Settings -> Source only chooses; it never moves music.
    panel_.open(Page::source);
    down(3);
    cross();
    EXPECT_FALSE(s.companion_action_pending);
    EXPECT_EQ(s.preferences.player, "kitchen");
}

TEST_F(AfterglowPanel, PlayOnThisPs5BringsTheMusicHere)
{
    auto &s = presentation();
    live();
    s.companion.queue_id = "room";
    s.companion.own_player_id = "ps5";
    s.companion.can_move_here = true;
    s.preferences.follow = true;
    panel_.open(Page::queue);
    down(1); // under Playing on
    cross();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::transfer);
    EXPECT_EQ(s.companion_action.text, "ps5");
    EXPECT_TRUE(s.companion_action.enabled) << "it starts here even if it had stopped";
    EXPECT_FALSE(s.preferences.follow) << "the PS5 now sounds it";

    // Playing on offers the same move as its first row.
    s.companion_action_pending = false;
    s.preferences.follow = true;
    panel_.open(Page::queue);
    cross();
    ASSERT_EQ(panel_.page(), Page::players);
    cross();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.text, "ps5");
    EXPECT_FALSE(s.preferences.follow);

    // Nothing to move: the row is the plain choice again.
    s.companion_action_pending = false;
    s.companion.can_move_here = false;
    s.preferences.follow = true;
    panel_.open(Page::players);
    cross();
    EXPECT_FALSE(s.companion_action_pending);
    EXPECT_FALSE(s.preferences.follow);

    // This PS5's own player in the speaker list is the same choice.
    s.preferences.follow = true;
    CompanionPlayer own{"ps5", "Afterglow PS5"};
    own.takes_music = true;
    s.companion.players = {own};
    panel_.open(Page::players);
    down(2); // Active speaker, then past the heading to this PS5's own player
    cross();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.text, "ps5");
    EXPECT_TRUE(s.companion_action.enabled);
    EXPECT_FALSE(s.preferences.follow) << "followed, its own player would play silently";
}

TEST_F(AfterglowPanel, JoinAndMoveHereComeFirstAndEachSpeakerSaysWhatItPlays)
{
    auto &s = presentation();
    live();
    CompanionPlayer room{"room", "Living room"};
    room.activity = "Low tide — Demo";
    CompanionPlayer own{"ps5", "Afterglow PS5"};
    own.activity = "Idle";
    own.takes_music = true;
    CompanionPlayer den{"den", "Den"};
    den.activity = "Paused · Second kettle";
    s.companion.players = {room, own, den};
    s.companion.queue_id = "room";
    s.companion.own_player_id = "ps5";
    s.companion.can_move_here = true;
    s.companion.can_join = true;
    s.companion.join_leader = "room";
    s.companion.join_leader_name = "Living room";
    s.preferences.follow = true;
    const auto value = [&](const std::string &title)
    {
        for (const auto &row : panel_.rows())
            if (row.item.title == title)
                return row.item.value;
        return std::string("(missing)");
    };
    panel_.open(Page::queue);
    ASSERT_GE(panel_.rows().size(), 3u);
    EXPECT_EQ(panel_.rows()[1].item.title, "Join Living room");
    EXPECT_EQ(panel_.rows()[1].item.value, "Plays here too");
    EXPECT_EQ(panel_.rows()[2].item.title, "Move here");
    EXPECT_EQ(panel_.rows()[2].item.value, "Moves the music from Living room");
    panel_.open(Page::players);
    EXPECT_EQ(panel_.rows()[0].item.title, "Join Living room");
    EXPECT_EQ(panel_.rows()[1].item.title, "Move here");
    EXPECT_EQ(value("This PS5"), "(missing)") << "Move here stands in for it";
    EXPECT_EQ(value("Living room"), "Low tide — Demo");
    EXPECT_EQ(value("Den"), "Paused · Second kettle");
    EXPECT_EQ(value("Afterglow PS5"), "This PS5 · Idle · moves the music here");
    cross();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::group_add);
    EXPECT_EQ(s.companion_action.option, "room");
    EXPECT_EQ(s.companion_action.text, "ps5");
    EXPECT_EQ(s.toast, "Joined Living room");
    EXPECT_FALSE(s.preferences.follow) << "the PS5 now sounds it through its own player";
    s.companion_action_pending = false;
    s.preferences.follow = true;
    s.companion.can_join = false;
    panel_.open(Page::players);
    EXPECT_EQ(panel_.rows()[0].item.title, "Move here") << "Join only while it is offered";
}

TEST_F(AfterglowPanel, SpeakersTogetherSetsTheVolumeOfOneMember)
{
    auto &s = presentation();
    live();
    CompanionPlayer lead{"lead", "Lead"};
    lead.group_members = {"lead", "kitchen", "hall"};
    lead.volume = 35;
    lead.volume_settable = true;
    CompanionPlayer kitchen{"kitchen", "Kitchen"};
    kitchen.volume = 20;
    kitchen.volume_settable = true;
    CompanionPlayer hall{"hall", "Hall"};
    s.companion.players = {lead, kitchen, hall};
    s.companion.queue_id = "lead";
    s.preferences.follow = true;
    panel_.open(Page::players);
    std::vector<std::string> volumes;
    for (const auto &row : panel_.rows())
        if (row.adjustable)
            volumes.push_back(row.item.title + " " + row.item.value);
    EXPECT_EQ(volumes, (std::vector<std::string>{"Lead 35%", "Kitchen 20%"}))
        << "the hall's volume is fixed; the group cannot be edited, yet its volumes show";
    // Active speaker, three speakers, the three ticks, then Lead's and Kitchen's volume.
    down(9);
    input(nav(Direction::right));
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::member_volume);
    EXPECT_EQ(s.companion_action.text, "kitchen");
    EXPECT_EQ(s.companion_action.value, 5);
    s.companion_action_pending = false;
    input(nav(Direction::left));
    EXPECT_EQ(s.companion_action.value, -5);
    s.companion_action_pending = false;
    s.companion.players[0].group_members = {"lead"};
    panel_.open(Page::players);
    for (const auto &row : panel_.rows())
        EXPECT_FALSE(row.adjustable) << "one speaker alone has the Queue's volume";
}

TEST_F(AfterglowPanel, SpeakersTogetherBelongsToTheTargetAndRefusesWhoCannotJoin)
{
    auto &s = presentation();
    live();
    CompanionPlayer lead{"lead", "Lead"};
    lead.can_set_members = true;
    lead.group_members = {"lead", "fixed"};
    lead.static_members = {"fixed"};
    lead.can_group_with = {"sendspin"};
    CompanionPlayer fixed{"fixed", "Fixed"};
    CompanionPlayer cousin{"cousin", "Cousin"};
    cousin.provider = "sendspin";
    CompanionPlayer stranger{"stranger", "Stranger"};
    s.companion.players = {lead, fixed, cousin, stranger};
    // Automatic following: the target is the queue followed, not a chosen speaker.
    s.preferences.follow = true;
    s.companion.queue_id = "lead";
    panel_.open(Page::players);
    down(8); // Active speaker, four speakers, then the leader, Fixed and Cousin together
    cross();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::group_add);
    EXPECT_EQ(s.companion_action.option, "lead");
    EXPECT_EQ(s.companion_action.text, "cousin");
    s.companion_action_pending = false;
    down(1); // Stranger: neither named nor of a named provider
    cross();
    EXPECT_FALSE(s.companion_action_pending);
    input(nav(Direction::up));
    input(nav(Direction::up)); // Fixed: always in the group
    cross();
    EXPECT_FALSE(s.companion_action_pending);
}

TEST_F(AfterglowPanel, CompensationSavesOnceAfterTheLastStep)
{
    auto &s = presentation();
    live();
    s.companion.static_delay_ms = 100;
    s.companion.own_player_id = "ps5";
    s.companion.static_delay_player = "ps5";
    s.companion.static_delay_key = "sendspin_static_delay";
    panel_.open(Page::settings);
    down(4);
    cross();
    ASSERT_EQ(panel_.page(), Page::sync);
    for (int i = 0; i < 3; ++i)
        input(nav(Direction::right));
    wait(0.5f);
    EXPECT_FALSE(s.companion_action_pending) << "debounced: nothing yet";
    input(nav(Direction::left));
    wait(0.5f);
    EXPECT_FALSE(s.companion_action_pending) << "every step restarts the wait";
    wait(0.2f);
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::static_delay);
    EXPECT_EQ(s.companion_action.value, 120);
    EXPECT_EQ(s.companion_action.text, "ps5");
    EXPECT_EQ(s.companion_action.option, "sendspin_static_delay");
    s.companion_action_pending = false;
    wait(1.0f);
    EXPECT_FALSE(s.companion_action_pending) << "one action per change";
}

TEST_F(AfterglowPanel, CompensationNeverCarriesOnePlayersDelayOntoAnother)
{
    auto &s = presentation();
    live();
    s.companion.static_delay_ms = 100;
    s.companion.static_delay_player = "old-player";
    s.companion.static_delay_key = "sendspin_static_delay";
    panel_.open(Page::sync);
    input(nav(Direction::right)); // 110, waiting to be saved
    input(idle());
    ASSERT_EQ(panel_.rows()[0].item.value, "Sound sent 110 ms early");
    // This PS5's player changes before the save; the new one's delay is read.
    s.companion.static_delay_player = "new-player";
    s.companion.static_delay_key = "up1||protocol||sendspin_static_delay";
    s.companion.static_delay_ms = 300;
    wait(0.5f); // the sheet re-reads its rows; the save is still 0.6 s after the step
    ASSERT_FALSE(s.companion_action_pending);
    EXPECT_EQ(panel_.rows()[0].item.value, "Sound sent 300 ms early")
        << "the new player shows its own delay";
    wait(0.7f);
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.value, 110);
    EXPECT_EQ(s.companion_action.text, "old-player") << "the change goes to its own player";
    EXPECT_EQ(s.companion_action.option, "sendspin_static_delay");
    const auto wire =
        hui::afterglow::companion_wire::action_request(s.companion_action, s.companion);
    EXPECT_EQ(wire.args,
              "{\"player_id\":\"old-player\",\"values\":{\"sendspin_static_delay\":110}}");
}

TEST_F(AfterglowPanel, CompensationSpeedsUpWhenHeldAndStaysInRange)
{
    auto &s = presentation();
    live();
    s.companion.static_delay_ms = 4900;
    panel_.open(Page::sync);
    input(nav(Direction::right));
    hui::InputFrame held = nav(Direction::right);
    held.nav_repeat = true;
    for (int i = 0; i < 12; ++i)
        input(held);
    wait(0.7f);
    EXPECT_EQ(s.companion_action.value, 5000);
    s.companion_action_pending = false;
    s.companion.static_delay_ms = 5000;
    wait(5.0f);
    hui::InputFrame left = nav(Direction::left);
    input(left);
    left.nav_repeat = true;
    for (int i = 0; i < 9; ++i)
        input(left);
    input(left); // the tenth repeat steps by 100 ms
    wait(0.7f);
    EXPECT_EQ(s.companion_action.value, 5000 - 10 * 10 - 100);
}

TEST_F(AfterglowPanel, CompensationRefusesWhileFollowingOrUnread)
{
    auto &s = presentation();
    live();
    panel_.open(Page::sync);
    input(nav(Direction::right));
    wait(0.7f);
    EXPECT_FALSE(s.companion_action_pending) << "the delay is not read yet";
    s.companion.static_delay_ms = 0;
    s.preferences.follow = true;
    input(nav(Direction::right));
    wait(0.7f);
    EXPECT_FALSE(s.companion_action_pending) << "only This PS5's own player";
    down(1);
    input(nav(Direction::right));
    EXPECT_EQ(s.preferences.trim_ms, 25) << "follow timing still adjusts";
    EXPECT_FALSE(panel_.back()) << "opened as the root";
}

TEST_F(AfterglowPanel, SyncFigurePlacesThePictureByTheTrimOnly)
{
    using hui::afterglow::sync_picture_at;
    EXPECT_FLOAT_EQ(sync_picture_at(0), 0.5f);
    EXPECT_FLOAT_EQ(sync_picture_at(-300), 0.25f);
    EXPECT_FLOAT_EQ(sync_picture_at(300), 0.75f);
    EXPECT_FLOAT_EQ(sync_picture_at(2000), 0.98f);
    EXPECT_FLOAT_EQ(sync_picture_at(-2000), 0.02f);
}

TEST_F(AfterglowPanel, SignInAsksAddressNameAndSecretPasswordInTurn)
{
    auto &s = presentation();
    s.account.host = "music.local";
    panel_.open(Page::settings);
    down(5);
    cross();
    ASSERT_EQ(panel_.page(), Page::account);
    cross();
    ASSERT_TRUE(s.keyboard_pending);
    EXPECT_EQ(s.keyboard.purpose, KeyboardPurpose::sign_in_host);
    EXPECT_EQ(s.keyboard.initial, "music.local") << "the configured host is offered";
    type(KeyboardPurpose::sign_in_host, "music.local ");
    input(idle());
    ASSERT_TRUE(s.keyboard_pending);
    EXPECT_EQ(s.keyboard.purpose, KeyboardPurpose::sign_in_username);
    type(KeyboardPurpose::sign_in_username, "ewa");
    input(idle());
    ASSERT_TRUE(s.keyboard_pending);
    EXPECT_EQ(s.keyboard.purpose, KeyboardPurpose::sign_in_password);
    EXPECT_TRUE(s.keyboard.secret);
    EXPECT_EQ(s.account_command, AccountCommand::none);
    type(KeyboardPurpose::sign_in_password, " pass word ");
    input(idle());
    EXPECT_EQ(s.account_command, AccountCommand::sign_in);
    EXPECT_EQ(s.sign_in_host, "music.local");
    EXPECT_EQ(s.sign_in_username, "ewa");
    EXPECT_EQ(s.sign_in_password, " pass word ") << "passwords are taken as typed";
    EXPECT_TRUE(s.keyboard_result.text.empty()) << "the password is not left behind";
    EXPECT_TRUE(panel_.back());
    EXPECT_EQ(panel_.page(), Page::settings);
}

TEST_F(AfterglowPanel, CancellingSignInStopsTheSequence)
{
    auto &s = presentation();
    panel_.open(Page::account);
    cross();
    type(KeyboardPurpose::sign_in_host, "music.local");
    input(idle());
    type(KeyboardPurpose::sign_in_username, "", false);
    s.keyboard_pending = false;
    input(idle());
    EXPECT_FALSE(s.keyboard_pending);
    EXPECT_EQ(s.account_command, AccountCommand::none);
    EXPECT_EQ(s.toast, "Sign-in cancelled");
}

TEST_F(AfterglowPanel, LogOutOnlyForAccountsSignedInHereAndSyncToggles)
{
    auto &s = presentation();
    s.account.source = AccountState::Source::config;
    panel_.open(Page::account);
    down(1);
    cross();
    EXPECT_EQ(s.account_command, AccountCommand::none) << "a config token cannot log out";
    s.account.source = AccountState::Source::signed_in;
    input(idle());
    cross();
    EXPECT_EQ(s.account_command, AccountCommand::log_out);
    down(1);
    cross();
    EXPECT_FALSE(s.preferences.account_sync);
}

TEST_F(AfterglowPanel, VisualsColourStrengthBeatAndControllerLight)
{
    auto &p = presentation().preferences;
    panel_.open(Page::visuals);
    down(2);
    cross(); // tint on: Colour strength appears below it
    down(1);
    input(nav(Direction::left));
    input(nav(Direction::left));
    input(nav(Direction::left));
    input(nav(Direction::left));
    EXPECT_EQ(p.tint_strength, 25);
    down(1);
    EXPECT_TRUE(p.cover_art) << "Cover art in the visuals is on by default";
    cross();
    EXPECT_FALSE(p.cover_art);
    down(1);
    cross();
    EXPECT_FALSE(p.night_register);
    // OLED, then Change preset: past 10 min to Each song and On big moments.
    down(2);
    for (int i = 0; i < 40; ++i)
        input(nav(Direction::right));
    EXPECT_EQ(p.preset_change, 2);
    EXPECT_EQ(p.preset_seconds, 600);
    input(nav(Direction::left));
    EXPECT_EQ(p.preset_change, 1);
    // Order, lock, favourites, skip slow, minimum fps, level, beat.
    down(7);
    for (int i = 0; i < 25; ++i)
        input(nav(Direction::right));
    EXPECT_EQ(p.beat_sensitivity, 200);
    down(4); // frame rate, hold, measure, then the light bar
    cross();
    EXPECT_FALSE(p.lightbar);
    down(1);
    cross();
    EXPECT_FALSE(p.lightbar_beat) << "no pulse without the light bar";
    input(nav(Direction::up));
    cross();
    input(idle());
    down(1);
    cross();
    EXPECT_TRUE(p.lightbar_beat);
}

TEST_F(AfterglowPanel, DisplayTogglesWordByWordLyrics)
{
    panel_.open(Page::display);
    down(11);
    cross();
    EXPECT_FALSE(presentation().preferences.karaoke);
}

TEST_F(AfterglowPanel, HomeDelegatesToItsSheet)
{
    panel_.open(Page::home);
    EXPECT_EQ(panel_.page(), Page::home);
    EXPECT_FALSE(panel_.covers_screen());
    input(press(Action::confirm));
    EXPECT_FALSE(panel_.back()) << "the stub has no deeper level";
    hui::gfx::DrawList list;
    hui::ui::Canvas canvas{list, fonts_, 0, 0};
    panel_.draw(canvas);
    EXPECT_FALSE(list.instances().empty());
}

TEST_F(AfterglowPanel, HomeJoinsTheSectionsOnlyWhenConfigured)
{
    auto &s = presentation();
    s.companion_configured = true;
    auto screen = hui::concepts::make_afterglow(context_);
    const auto step = [&](const hui::InputFrame &frame)
    { screen->update(frame, 0.02f, feedback_); };
    step(press(Action::menu));
    step(press(Action::page_next)); // Queue
    step(press(Action::page_next)); // Settings
    step(press(Action::page_next)); // Browse
    EXPECT_TRUE(s.browse_pending);
    step(press(Action::menu));
    s.browse_pending = false;
    s.home.configured = true;
    step(press(Action::menu));
    step(press(Action::page_next)); // Queue
    step(press(Action::page_next)); // Settings
    step(press(Action::page_next)); // Home
    EXPECT_FALSE(s.browse_pending);
    step(press(Action::page_next)); // Browse
    EXPECT_TRUE(s.browse_pending);
}
TEST_F(AfterglowPanel, NamesTheOpenPageForDevStatus)
{
    panel_.open(Page::queue);
    EXPECT_STREQ(panel_.page_name(), "queue");
    panel_.open(Page::home);
    EXPECT_STREQ(panel_.page_name(), "home");
    auto screen = hui::concepts::make_afterglow(context_);
    screen->update(press(Action::menu), 0.02f, feedback_);
    EXPECT_EQ(presentation().page, "");
    screen->update(press(Action::page_next), 0.02f, feedback_);
    screen->update(idle(), 0.02f, feedback_);
    EXPECT_EQ(presentation().page, "queue");
}
} // namespace
