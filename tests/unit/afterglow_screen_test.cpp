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
    EXPECT_TRUE(presentation().companion_action_pending)
        << "with the controls gone, right seeks instead of moving a tile";
}

TEST_F(AfterglowMain, DpadSeeksTenSecondsAndNamesIt)
{
    playing_song();
    step(nav(Direction::right));
    auto &s = presentation();
    ASSERT_TRUE(s.companion_action_pending);
    EXPECT_EQ(s.companion_action.kind, CompanionActionKind::seek);
    EXPECT_EQ(s.companion_action.value, 70);
    EXPECT_NE(s.toast.find("Ahead 10 s"), std::string::npos);
}

TEST_F(AfterglowMain, SeekingALiveStreamExplainsInsteadOfActing)
{
    playing_song();
    presentation().companion.track.duration = 0;
    step(nav(Direction::left));
    EXPECT_FALSE(presentation().companion_action_pending);
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

TEST_F(AfterglowMain, AlbumPosterListsTheNextTwoSongs)
{
    playing_song();
    auto &s = presentation();
    s.preferences.display_mode = 2;
    hui::afterglow::CompanionTrack next;
    next.title = "Evening";
    next.artist = "Someone";
    s.companion.upcoming = {next};
    const auto drawn = [&]
    {
        step(idle(), 1.0f);
        hui::app::Frame frame;
        screen_->draw(frame);
        return glyphs(frame);
    };
    const int one = drawn();
    next.title = "Still Water";
    s.companion.upcoming.push_back(next);
    s.companion.upcoming.push_back(next);
    EXPECT_EQ(drawn() - one, 10 + 7) << "the second title and artist, never a third";
    s.companion.track.quality_badge = "Lossless";
    s.companion.track.format = "flac / 44100 Hz / 16 bit";
    const int badge = drawn();
    s.companion.track.quality_badge.clear();
    EXPECT_GT(drawn(), badge) << "the badge replaces the longer format line";
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
