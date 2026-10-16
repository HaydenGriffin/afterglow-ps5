// Afterglow - Controller map, lyric layers and controls overlay of the main screen.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/presentation.hpp"
#include "afterglow/scenes.hpp"
#include "app/shell.hpp"
#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"

namespace
{
using hui::Action;
using hui::Direction;
using hui::afterglow::CompanionActionKind;
using hui::afterglow::presentation;
using hui::afterglow::UiCommand;

class AfterglowMain : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        presentation() = {};
        screen_ = hui::concepts::make_afterglow(context_);
    }
    void TearDown() override
    {
        presentation() = {};
    }
    void step(const hui::InputFrame &input, float dt = 0.02f)
    {
        screen_->update(input, dt, feedback_);
    }
    void playing_song()
    {
        auto &s = presentation();
        s.configured = s.connected = s.playing = true;
        s.title = "Slow Current";
        s.companion.connected = true;
        s.companion.track.title = s.title;
        s.companion.track.uri = "library://track/7";
        s.companion.track.duration = 240;
        s.companion.elapsed = 60;
    }
    std::unique_ptr<hui::app::Concept> screen_;
};

TEST_F(AfterglowMain, OptionsOpensTransportWithPauseFocusedAndCircleCloses)
{
    playing_song();
    step(press(Action::menu));
    step(press(Action::confirm));
    EXPECT_EQ(presentation().command, UiCommand::toggle);
    presentation().command = UiCommand::none;
    step(press(Action::back));
    step(press(Action::confirm));
    EXPECT_EQ(presentation().command, UiCommand::toggle) << "Cross plays/pauses once closed too";
}

TEST_F(AfterglowMain, TransportClosesItselfAfterNineIdleSeconds)
{
    playing_song();
    step(press(Action::menu));
    for (int i = 0; i < 500; ++i)
        step(idle());
    step(nav(Direction::right));
    EXPECT_EQ(presentation().seek_seconds, 10)
        << "with the controls gone, right seeks instead of moving a tile";
}

TEST_F(AfterglowMain, DpadSeeksTenSecondsAndNamesIt)
{
    playing_song();
    step(nav(Direction::right));
    auto &s = presentation();
    EXPECT_EQ(s.seek_seconds, 10);
    EXPECT_FALSE(s.companion_action_pending) << "seeks go through Companion::seek_by";
    EXPECT_EQ(s.toast, "Ahead 10 s · 1:10");
}

TEST_F(AfterglowMain, QuickSeeksAddUpAndTheToastFollowsThem)
{
    playing_song();
    auto &s = presentation();
    s.playing = s.companion.playing = false;
    step(nav(Direction::right));
    step(nav(Direction::right));
    step(nav(Direction::right));
    EXPECT_EQ(s.seek_seconds, 30) << "none dropped while the first is in flight";
    EXPECT_EQ(s.toast, "Ahead 10 s · 1:30") << "counted from the last press, not the stale report";
    step(nav(Direction::left));
    EXPECT_EQ(s.seek_seconds, 20);
    EXPECT_EQ(s.toast, "Back 10 s · 1:20");
    s.seek_seconds = 0; // main.cpp handed it on
    step(idle(), 5.0f);
    step(nav(Direction::left));
    EXPECT_EQ(s.toast, "Back 10 s · 0:50") << "once the report is due, it is the base again";
}

TEST_F(AfterglowMain, SeekingALiveStreamExplainsInsteadOfActing)
{
    playing_song();
    presentation().companion.track.duration = 0;
    step(nav(Direction::left));
    EXPECT_EQ(presentation().seek_seconds, 0);
    EXPECT_EQ(presentation().toast, "This stream can't seek");
}

TEST_F(AfterglowMain, TriangleCyclesLyricLayersOverVisualsAndTogglesPosterLyrics)
{
    playing_song();
    auto &p = presentation().preferences;
    presentation().companion.track.lrc_lyrics = "[00:00]One\n[01:00]Two";
    step(idle());
    step(press(Action::north));
    EXPECT_EQ(p.lyrics_layer, 1);
    EXPECT_EQ(presentation().toast, "Lyrics: one line");
    step(press(Action::north));
    EXPECT_EQ(p.lyrics_layer, 2);
    step(press(Action::north));
    EXPECT_EQ(p.lyrics_layer, 0);
    p.display_mode = 2;
    step(press(Action::north));
    EXPECT_EQ(p.display_mode, 3);
    step(press(Action::north));
    EXPECT_EQ(p.display_mode, 2);
}

TEST_F(AfterglowMain, SquareCyclesDisplayModesWithANamedToast)
{
    step(press(Action::west));
    EXPECT_EQ(presentation().preferences.display_mode, 2);
    EXPECT_EQ(presentation().toast, "Album");
}

TEST_F(AfterglowMain, TouchpadNoLongerTogglesMotion)
{
    step(press(Action::touch));
    EXPECT_FALSE(presentation().preferences.reduce_motion);
    EXPECT_FALSE(settings_.reduced_motion);
    presentation().preferences.reduce_motion = true;
    step(idle());
    EXPECT_TRUE(settings_.reduced_motion) << "the Display setting drives the kit's flag";
}

TEST_F(AfterglowMain, L3AndR3ConfirmPresetChanges)
{
    presentation().preset = "Flexi - mindblob";
    step(press(Action::l3));
    EXPECT_TRUE(presentation().preferences.locked);
    EXPECT_EQ(presentation().toast, "Preset locked");
    step(press(Action::r3));
    EXPECT_TRUE(presentation().preferences.favorite("Flexi - mindblob"));
    EXPECT_EQ(presentation().toast, "Favourite preset");
}

TEST_F(AfterglowMain, CloseHoldUsesElapsedTimeOutsideAndInsideControls)
{
    for (bool controls : {false, true})
    {
        presentation() = {};
        screen_ = hui::concepts::make_afterglow(context_);
        if (controls)
            step(press(Action::menu));
        auto held = idle();
        held.held = hui::action_bit(Action::back);
        for (int i = 0; i < 40; ++i)
            step(held);
        EXPECT_FALSE(presentation().exit_requested) << "controls=" << controls;
        for (int i = 0; i < 40; ++i)
            step(held);
        EXPECT_TRUE(presentation().exit_requested) << "controls=" << controls;
    }
}

TEST_F(AfterglowMain, SectionsMoveBetweenSheetsAndBackToTheTransport)
{
    playing_song();
    step(press(Action::menu));
    step(press(Action::page_next)); // Queue
    step(press(Action::page_next)); // Settings
    step(press(Action::page_next)); // Browse
    step(press(Action::page_next)); // Now playing: the transport again
    step(press(Action::confirm));
    EXPECT_EQ(presentation().command, UiCommand::toggle);
}

TEST_F(AfterglowMain, EveryStateDrawsWithoutTouchingGl)
{
    playing_song();
    presentation().companion.track.lrc_lyrics = "[00:00]One\n[00:50]Two\n[01:30]Three";
    for (int mode = 0; mode < 4; ++mode)
        for (int layer = 0; layer < 3; ++layer)
        {
            presentation().preferences.display_mode = mode;
            presentation().preferences.lyrics_layer = layer;
            for (int i = 0; i < 30; ++i)
                step(idle(), 0.4f);
            hui::app::Frame frame;
            screen_->draw(frame);
            EXPECT_FALSE(frame.scene.empty()) << "mode=" << mode << " layer=" << layer;
        }
    presentation() = {};
    hui::app::Frame frame;
    screen_->draw(frame);
    EXPECT_FALSE(frame.scene.empty()) << "first run draws its setup poster";
}

// Glyph quads drawn with an alpha inside (low, high): a way to tell text
// layers apart without OpenGL.
int glyphs(const hui::app::Frame &frame, float low = 0, float high = 2)
{
    int count = 0;
    for (const auto &instance : frame.scene.instances())
        if (static_cast<hui::gfx::Shape>(static_cast<int>(instance.params[3])) ==
                hui::gfx::Shape::glyph &&
            instance.color_top[3] > low && instance.color_top[3] < high)
            ++count;
    return count;
}

TEST_F(AfterglowMain, KaraokeDimsTheUnsungWordsOfTheLyricPoster)
{
    playing_song();
    auto &s = presentation();
    s.companion.track.lrc_lyrics = "[00:50]<00:50>Hold <00:55>on <01:05>tight\n[01:30]Later";
    s.preferences.display_mode = 3;
    const auto unsung = [&](bool karaoke)
    {
        s.preferences.karaoke = karaoke;
        for (int i = 0; i < 10; ++i)
            step(idle(), 0.1f);
        hui::app::Frame frame;
        screen_->draw(frame);
        return glyphs(frame, 0.3f, 0.5f);
    };
    EXPECT_GE(unsung(true) - unsung(false), 5) << "\"tight\" waits its turn";
}

TEST_F(AfterglowMain, NightRegisterStepsTheFieldsDownOnlyWhenAllowed)
{
    auto &s = presentation();
    s.artwork_color = {0.16f, 0.25f, 0.72f};
    const auto day = hui::afterglow::field_a(s);
    s.night_hours = true;
    s.preferences.night_register = false;
    EXPECT_FLOAT_EQ(hui::afterglow::field_a(s).b, day.b) << "switched off in preferences";
    s.preferences.night_register = true;
    EXPECT_TRUE(hui::afterglow::night_register(s));
    EXPECT_NEAR(hui::afterglow::field_a(s).b, day.b * 0.72f, 0.001f);
    EXPECT_LT(hui::afterglow::field_b(s).b, day.b);
}

TEST_F(AfterglowMain, AlbumPosterNamesTheNextSongAndItsArtists)
{
    playing_song();
    auto &s = presentation();
    s.preferences.display_mode = 2;
    hui::afterglow::CompanionTrack next;
    next.title = "Evening";
    next.artist = "Someone";
    s.companion.next_up = {next};
    const auto drawn = [&]
    {
        step(idle(), 1.0f);
        hui::app::Frame frame;
        screen_->draw(frame);
        return glyphs(frame);
    };
    const int one = drawn();
    next.title = "Still Water";
    s.companion.next_up.push_back(next);
    s.companion.next_up.push_back(next);
    EXPECT_EQ(drawn() - one, 0) << "one song and its artists, as LG's Up next line";
    const int plain = drawn();
    s.companion.track.quality_badge = "Lossless";
    s.companion.track.format = "flac / 44100 Hz / 16 bit";
    EXPECT_EQ(drawn(), plain) << "format and quality live in Details, not on the poster (LG 1.5.0)";
}

TEST_F(AfterglowMain, PosterHasNoSongChangeSlide)
{
    playing_song();
    auto &s = presentation();
    s.preferences.display_mode = 2;
    const auto frame_after = [&](float seconds)
    {
        step(idle(), seconds);
        auto frame = std::make_unique<hui::app::Frame>();
        screen_->draw(*frame);
        return frame;
    };
    frame_after(1.0f);
    s.title = s.companion.track.title = "A New Song";
    const auto fresh = frame_after(0.02f);
    const auto settled = frame_after(1.0f);
    EXPECT_EQ(fresh->scene.instances().size(), settled->scene.instances().size())
        << "no cover slides off the fields when the song changes (LG 1.5.0)";
}

// Music Assistant answering with a paused song and recent music.
void paused_with_recent()
{
    auto &s = presentation();
    s.configured = s.connected = true;
    s.companion_configured = true;
    s.companion.connected = true;
    s.companion.queue_state = "paused";
    s.companion.track.title = "Slow Current";
    s.companion.track.uri = "library://track/7";
    s.companion.track.artwork = "http://192.0.2.1/now.png";
    s.companion.target_name = "Listening room";
    hui::afterglow::CompanionTrack album;
    album.title = "Harbour Lamps";
    album.uri = "library://album/3";
    album.media_type = "album";
    album.artwork = "http://192.0.2.1/album.png";
    s.companion.recent = {album};
    s.companion.recent_state = hui::afterglow::RecentState::ready;
}

TEST_F(AfterglowMain, IdlePosterSetsThePausedSongAndPlaysRecentMusicOnTheTarget)
{
    paused_with_recent();
    auto &s = presentation();
    for (int i = 0; i < 29 * 5; ++i)
        step(idle(), 0.2f);
    EXPECT_FALSE(s.idle_showing) << "a pause waits half a minute";
    for (int i = 0; i < 10; ++i)
        step(idle(), 0.2f);
    ASSERT_TRUE(s.idle_showing);
    EXPECT_TRUE(s.recent_pending) << "recent music is read as it appears";
    EXPECT_EQ(s.idle_artwork, "http://192.0.2.1/now.png");
    EXPECT_FALSE(s.milkdrop_visible) << "MilkDrop rests behind the idle poster";
    {
        hui::app::Frame frame;
        screen_->draw(frame);
        EXPECT_FALSE(frame.scene.empty());
    }

    step(press(Action::confirm));
    EXPECT_EQ(s.command, UiCommand::toggle) << "Cross resumes the paused song";
    s.command = UiCommand::none;
    step(nav(Direction::right));
    EXPECT_FALSE(s.companion_action_pending) << "right walks the strip instead of seeking";
    step(idle());
    EXPECT_EQ(s.idle_artwork, "http://192.0.2.1/album.png");
    step(press(Action::confirm));
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::play_media);
    EXPECT_EQ(s.companion_action.text, "library://album/3");
    EXPECT_EQ(s.companion_action.option, "replace");
    s.companion_action_pending = false;

    step(nav(Direction::right));
    step(press(Action::confirm));
    step(idle());
    EXPECT_EQ(s.page, "browse") << "Browse closes the strip, so Cross never dead-ends";
    EXPECT_FALSE(s.idle_showing);
}

TEST_F(AfterglowMain, IdlePosterGivesWayToTheControlsAndToMusic)
{
    paused_with_recent();
    auto &s = presentation();
    s.companion.queue_state = "idle"; // stopped: eight seconds are enough
    for (int i = 0; i < 9 * 5; ++i)
        step(idle(), 0.2f);
    ASSERT_TRUE(s.idle_showing);
    step(press(Action::menu));
    step(idle());
    EXPECT_FALSE(s.idle_showing) << "Options opens the transport band as usual";
    step(press(Action::back));
    step(idle());
    EXPECT_TRUE(s.idle_showing);
    s.companion.playing = true;
    s.companion.queue_state = "playing";
    step(idle());
    EXPECT_FALSE(s.idle_showing);
    EXPECT_TRUE(s.idle_artwork.empty()) << "the song's own cover is loaded again";
}

TEST_F(AfterglowMain, HostShellForwardsShoulderButtonsToAfterglow)
{
    hui::app::Shell shell(fonts_, catalog_, "/dev/null", 0);
    std::size_t index = 0;
    while (index < shell.concept_count() &&
           std::string_view(shell.concept_at(index).info().id) != "afterglow")
        ++index;
    ASSERT_LT(index, shell.concept_count());
    shell.show(index, false);
    shell.update(press(Action::menu), 0.02f);
    shell.update(press(Action::page_next), 0.02f);
    EXPECT_EQ(shell.current(), index);
    shell.update(press(Action::page_prev), 0.02f);
    EXPECT_EQ(shell.current(), index);
}
} // namespace
