// Afterglow - Tests for preset changes on a timer, each song and on big moments.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/preset_change.hpp"
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

using namespace hui::afterglow;

namespace
{
// Feeds `seconds` of a constant energy in 1/60 s frames; returns how many moments fired.
int feed(MomentDetector &detector, double energy, double seconds)
{
    int hits = 0;
    for (int frame = 0; frame < static_cast<int>(seconds * 60); ++frame)
        hits += detector.push(energy, 1.0 / 60) ? 1 : 0;
    return hits;
}

// The host preview's music: a kick every half second, a bass line and hiss.
double steady_music_energy(int frame)
{
    constexpr int kRate = 48000, kFrames = kRate / 60;
    std::vector<float> stereo(kFrames * 2);
    for (int i = 0; i < kFrames; ++i)
    {
        const double t = static_cast<double>(frame * kFrames + i) / kRate;
        const double beat = std::fmod(t, 0.5);
        const double kick = std::sin(2 * M_PI * 55 * beat) * std::exp(-beat * 12);
        const double bass = 0.35 * std::sin(2 * M_PI * (std::fmod(t, 2.0) < 1 ? 110 : 82.4) * t);
        stereo[static_cast<std::size_t>(i) * 2] = stereo[static_cast<std::size_t>(i) * 2 + 1] =
            static_cast<float>(0.7 * kick + bass);
    }
    return pcm_energy(stereo.data(), kFrames);
}
} // namespace

TEST(AfterglowPresetChange, RowStepsThroughTimersThenSongsThenMoments)
{
    Preferences p;
    EXPECT_EQ(preset_change_label(p), "Every 2 min");
    p.preset_seconds = 585;
    EXPECT_EQ(preset_change_label(p), "Every 9 min 45 s");
    EXPECT_TRUE(step_preset_change(p, 1));
    EXPECT_EQ(preset_change_label(p), "Every 10 min");
    EXPECT_TRUE(step_preset_change(p, 1));
    EXPECT_EQ(preset_change_label(p), "Each song");
    EXPECT_TRUE(step_preset_change(p, 1));
    EXPECT_EQ(preset_change_label(p), "On big moments");
    EXPECT_FALSE(step_preset_change(p, 1)) << "the right end refuses";
    EXPECT_TRUE(step_preset_change(p, -1));
    EXPECT_TRUE(step_preset_change(p, -1));
    EXPECT_EQ(preset_change_label(p), "Every 10 min");
    p.preset_seconds = 15;
    EXPECT_EQ(preset_change_label(p), "Every 15 s");
    EXPECT_FALSE(step_preset_change(p, -1)) << "the left end refuses";
}

TEST(AfterglowPresetChange, PreferencesKeepAndSyncTheNewChoices)
{
    Preferences p;
    EXPECT_TRUE(p.cover_art);
    EXPECT_EQ(p.preset_change, 0);
    p.cover_art = false;
    p.preset_change = 2;
    const auto back = Preferences::decode(p.encode());
    EXPECT_FALSE(back.cover_art);
    EXPECT_EQ(back.preset_change, 2);
    EXPECT_EQ(Preferences::decode("preset_change=9\n").preset_change, 0);
    const auto synced = Preferences{}.with_synced(p.encode_synced());
    EXPECT_FALSE(synced.cover_art);
    EXPECT_EQ(synced.preset_change, 2);
}

TEST(AfterglowPresetChange, SongKeyPrefersTheQueueItem)
{
    EXPECT_EQ(song_key("q1", "Title", "Artist"), "q1");
    EXPECT_EQ(song_key("", "Title", "Artist"), "Title\nArtist");
    EXPECT_EQ(song_key("", "", "Artist"), "");
}

TEST(AfterglowPresetChange, EachSongWaitsForTheSongAndTheGap)
{
    SongGate gate;
    gate.song("a", 0);
    EXPECT_FALSE(gate.due(5000)) << "the first song keeps the preset";
    gate.song("b", 20000);
    EXPECT_FALSE(gate.due(21000));
    EXPECT_TRUE(gate.due(22000));
    EXPECT_FALSE(gate.due(40000)) << "once per song";
    gate.song("c", 50000);
    gate.song("d", 50500);
    gate.song("e", 51000);
    EXPECT_FALSE(gate.due(52500)) << "skipping through waits for the last";
    EXPECT_TRUE(gate.due(53000));
    gate.changed(59000);
    gate.song("f", 60000);
    EXPECT_FALSE(gate.due(62000)) << "10 s since the last change";
    EXPECT_TRUE(gate.due(69000));
    gate.song("g", 80000);
    gate.song("", 80500);
    gate.song("g", 90000);
    EXPECT_FALSE(gate.due(100000)) << "stopping drops a song still settling";
}

TEST(AfterglowPresetChange, SteadyMusicIsNoMoment)
{
    MomentDetector detector;
    int hits = 0;
    for (int frame = 0; frame < 60 * 120; ++frame)
        hits += detector.push(steady_music_energy(frame), 1.0 / 60) ? 1 : 0;
    EXPECT_EQ(hits, 0);
}

TEST(AfterglowPresetChange, QuietThenLoudIsOneMomentAndReArmsAfterAFall)
{
    MomentDetector detector;
    EXPECT_EQ(feed(detector, 0.003, 15), 0);
    EXPECT_EQ(feed(detector, 0.03, 0.3), 1) << "found within a fraction of a second";
    EXPECT_EQ(feed(detector, 0.03, 30), 0) << "staying loud is not another";
    feed(detector, 0.002, 15);
    EXPECT_EQ(feed(detector, 0.03, 5), 1);
}

TEST(AfterglowPresetChange, SilenceAPauseOrQuietMusicIsNoMoment)
{
    MomentDetector detector;
    EXPECT_EQ(feed(detector, 0.03, 20), 0) << "music out of silence";
    feed(detector, 0, 60);
    EXPECT_EQ(feed(detector, 0.03, 20), 0) << "after a pause";
    MomentDetector gap;
    feed(gap, 0.03, 30);
    feed(gap, 0, 1.5);
    EXPECT_EQ(feed(gap, 0.04, 10), 0) << "a short gap between songs";
    MomentDetector quiet;
    feed(quiet, 0.00005, 15);
    EXPECT_EQ(feed(quiet, 0.0005, 10), 0) << "too quiet to count";
}

TEST(AfterglowPresetChange, ChangerLetsAMomentInTheGapGo)
{
    PresetChanger changer;
    changer.changed(0);
    for (int i = 0; i < 15 * 60; ++i)
        changer.hear(0.003, 1.0 / 60);
    for (int i = 0; i < 60; ++i)
        changer.hear(0.03, 1.0 / 60);
    EXPECT_FALSE(changer.due(PresetChange::big_moments, 16000));
    EXPECT_FALSE(changer.due(PresetChange::big_moments, 30000));
    for (int i = 0; i < 15 * 60; ++i)
        changer.hear(0.002, 1.0 / 60);
    for (int i = 0; i < 60; ++i)
        changer.hear(0.03, 1.0 / 60);
    EXPECT_TRUE(changer.due(PresetChange::big_moments, 47000));
    EXPECT_FALSE(changer.due(PresetChange::timer, 90000));
}

TEST(AfterglowPresetChange, ChangerDropsSongsSeenUnderAnotherMode)
{
    PresetChanger changer;
    changer.song("a", 0);
    changer.song("b", 20000);
    EXPECT_FALSE(changer.due(PresetChange::timer, 23000));
    EXPECT_FALSE(changer.due(PresetChange::each_song, 60000));
    changer.song("c", 70000);
    EXPECT_TRUE(changer.due(PresetChange::each_song, 72000));
}
