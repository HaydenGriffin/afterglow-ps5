// Afterglow - The idle poster's strip, words, timers, dim register and Cross.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/idle.hpp"
#include <gtest/gtest.h>

namespace
{
using namespace hui::afterglow;

CompanionTrack item(const char *title, const char *uri, const char *type = "track")
{
    CompanionTrack t;
    t.title = title;
    t.uri = uri;
    t.media_type = type;
    return t;
}
std::vector<CompanionTrack> recent(int count)
{
    std::vector<CompanionTrack> list;
    for (int i = 0; i < count; ++i)
        list.push_back(item("Recent", ("library://album/" + std::to_string(i)).c_str(), "album"));
    return list;
}

TEST(AfterglowIdleTiles, PausedSongFirstRecentWithoutItThenBrowseLast)
{
    auto paused = item("Slow Current", "library://track/1");
    auto list = recent(3);
    list[1] = paused;
    const auto tiles = idle_tiles(&paused, list, RecentState::ready);
    ASSERT_EQ(tiles.size(), 4U);
    EXPECT_EQ(tiles[0].kind, IdleTileKind::resume);
    EXPECT_EQ(tiles[1].kind, IdleTileKind::play);
    EXPECT_EQ(tiles[2].item.uri, "library://album/2") << "the paused song is not offered twice";
    EXPECT_EQ(tiles.back().kind, IdleTileKind::browse);

    const auto many = idle_tiles(nullptr, recent(12), RecentState::ready);
    EXPECT_EQ(many.size(), static_cast<std::size_t>(companion_wire::kRecentLimit) + 1);
    const auto failed = idle_tiles(nullptr, {}, RecentState::error);
    ASSERT_EQ(failed.size(), 2U);
    EXPECT_EQ(failed[0].kind, IdleTileKind::retry);
    const auto nothing = idle_tiles(nullptr, {}, RecentState::ready);
    ASSERT_EQ(nothing.size(), 1U) << "Cross never dead-ends";
    EXPECT_EQ(nothing[0].kind, IdleTileKind::browse);
}

TEST(AfterglowIdleTiles, WordsFollowLg)
{
    auto paused = item("Slow Current", "library://track/1");
    paused.artist = "Lightkeeper";
    paused.duration = 225;
    const auto tiles = idle_tiles(&paused, recent(1), RecentState::ready);
    const auto resume = idle_text(tiles[0], RecentState::ready, true);
    EXPECT_EQ(resume.title, "Slow Current");
    EXPECT_EQ(resume.detail, "Paused · Lightkeeper · 3:45");
    EXPECT_EQ(resume.hint, "Resume");
    const auto play = idle_text(tiles[1], RecentState::ready, true);
    EXPECT_EQ(play.detail, "Album");
    EXPECT_EQ(play.hint, "Play");
    IdleTile browse;
    EXPECT_EQ(idle_text(browse, RecentState::ready, true).title, "Browse");
    EXPECT_EQ(idle_text(browse, RecentState::ready, false).detail,
              "Play something in Music Assistant and it shows here");
    EXPECT_EQ(idle_text(browse, RecentState::loading, false).detail, "Finding what you played…");
    IdleTile retry;
    retry.kind = IdleTileKind::retry;
    EXPECT_EQ(idle_text(retry, RecentState::error, false).hint, "Try again");
}

class AfterglowIdlePoster : public ::testing::Test
{
  protected:
    // Seconds of frames at 20 Hz; true if any frame asked for recent music.
    bool run(float seconds)
    {
        bool load = false;
        for (int i = 0, frames = static_cast<int>(seconds * 20 + 0.5f); i < frames; ++i)
            load = idle_.update(0.05f, in_) || load;
        return load;
    }
    IdlePoster::Inputs quiet(bool paused = false)
    {
        IdlePoster::Inputs in;
        in.paused = paused;
        in.allowed = true;
        in.recent = RecentState::ready;
        in.tiles = 3;
        return in;
    }
    IdlePoster idle_;
    IdlePoster::Inputs in_;
    std::vector<IdleTile> tiles_ = idle_tiles(nullptr, recent(2), RecentState::ready);
};

TEST_F(AfterglowIdlePoster, ShowsEightSecondsAfterTheMusicOrThirtyAfterAPause)
{
    in_ = quiet();
    run(7.8f);
    EXPECT_FALSE(idle_.showing());
    EXPECT_TRUE(run(0.4f)) << "recent music is read as it appears";
    EXPECT_TRUE(idle_.showing());
    in_.playing = true;
    run(0.05f);
    EXPECT_FALSE(idle_.showing()) << "music hides it at once";

    in_ = quiet(true);
    run(29.8f);
    EXPECT_FALSE(idle_.showing()) << "a pause is often a short one";
    run(0.4f);
    EXPECT_TRUE(idle_.showing());
    in_.allowed = false;
    run(0.05f);
    EXPECT_FALSE(idle_.showing()) << "the transport band or a sheet hides it";
    in_.allowed = true;
    EXPECT_FALSE(run(0.05f)) << "back at once, its list still fresh";
    EXPECT_TRUE(idle_.showing());
    run(61);
    in_.allowed = false;
    run(0.05f);
    in_.allowed = true;
    EXPECT_TRUE(run(0.05f)) << "a minute later the list is read again";
}

TEST_F(AfterglowIdlePoster, DimsAfterThreeQuietMinutesAndTheFirstKeyOnlyWakes)
{
    in_ = quiet();
    run(9);
    ASSERT_TRUE(idle_.showing());
    run(170);
    EXPECT_FALSE(idle_.resting());
    run(10);
    EXPECT_TRUE(idle_.resting());
    run(5);
    EXPECT_NEAR(idle_.level(), IdlePoster::kRestLevel, 0.01f);
    IdleAct act = IdleAct::play;
    EXPECT_TRUE(idle_.press(IdleKey::confirm, tiles_, act));
    EXPECT_EQ(act, IdleAct::none) << "the first press only wakes";
    EXPECT_FALSE(idle_.resting());
    run(0.05f);
    EXPECT_GT(idle_.level(), IdlePoster::kRestLevel);
    run(181);
    ASSERT_TRUE(idle_.resting());
    EXPECT_FALSE(idle_.press(IdleKey::other, tiles_, act))
        << "Options or Square wake it and keep their meaning";
    EXPECT_FALSE(idle_.resting());

    idle_.set_reduced_motion(true);
    run(181);
    EXPECT_FLOAT_EQ(idle_.level(), IdlePoster::kRestLevel) << "no fade with reduce motion";
}

TEST_F(AfterglowIdlePoster, LeftAndRightWalkTheStripAndCrossActsOnTheTile)
{
    in_ = quiet();
    run(9);
    IdleAct act = IdleAct::none;
    EXPECT_TRUE(idle_.press(IdleKey::left, tiles_, act));
    EXPECT_EQ(idle_.focus(), 0) << "the left end holds";
    ASSERT_TRUE(idle_.press(IdleKey::confirm, tiles_, act));
    EXPECT_EQ(act, IdleAct::play);
    idle_.press(IdleKey::right, tiles_, act);
    idle_.press(IdleKey::right, tiles_, act);
    idle_.press(IdleKey::right, tiles_, act);
    EXPECT_EQ(idle_.focus(), 2) << "the right end holds";
    idle_.press(IdleKey::confirm, tiles_, act);
    EXPECT_EQ(act, IdleAct::browse);

    auto paused = item("Slow Current", "library://track/1");
    const auto with_pause = idle_tiles(&paused, {}, RecentState::error);
    idle_.press(IdleKey::left, with_pause, act);
    idle_.press(IdleKey::left, with_pause, act);
    idle_.press(IdleKey::confirm, with_pause, act);
    EXPECT_EQ(act, IdleAct::resume);
    idle_.press(IdleKey::right, with_pause, act);
    idle_.press(IdleKey::confirm, with_pause, act);
    EXPECT_EQ(act, IdleAct::retry);
    EXPECT_FALSE(idle_.press(IdleKey::other, with_pause, act));
}

TEST_F(AfterglowIdlePoster, AStartThatDoesNotTakeSaysSo)
{
    in_ = quiet();
    run(9);
    idle_.starting("Starting…");
    run(9);
    EXPECT_EQ(idle_.note(), "Starting…");
    run(1.5f);
    EXPECT_EQ(idle_.note(), "Couldn’t start it. Cross tries again");
    in_.playing = true;
    run(0.05f);
    EXPECT_TRUE(idle_.note().empty());
}

TEST_F(AfterglowIdlePoster, NothingShowsWhileItIsNotAllowed)
{
    in_ = quiet();
    in_.allowed = false;
    EXPECT_FALSE(run(60));
    EXPECT_FALSE(idle_.showing());
    IdleAct act = IdleAct::none;
    EXPECT_FALSE(idle_.press(IdleKey::confirm, tiles_, act));
    in_.allowed = true;
    run(0.05f);
    EXPECT_TRUE(idle_.showing()) << "the quiet time kept counting";
}
} // namespace
