// Afterglow - Persistent visual preference regression tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/preferences.hpp"
#include "afterglow/preference_store.hpp"
#include "core/save_file.hpp"
#include <gtest/gtest.h>
#include <unistd.h>

using hui::afterglow::Preferences;

TEST(AfterglowPreferences, RoundTripPreservesAllControlsAndPresetNames)
{
    Preferences p;
    p.follow = p.auto_level = p.locked = p.favorites_only = p.tint = true;
    p.hold_idle = p.oled_drift = false;
    p.brightness = 70;
    p.text_brightness = 80;
    p.fps = 30;
    p.trim_ms = -175;
    p.preset_seconds = 45;
    p.track_info = 2;
    p.player = "speaker-kitchen";
    p.sequential = true;
    p.skip_slow = false;
    p.minimum_fps = 45;
    p.scan_cursor = 123;
    p.quality = R"({"pending":"preset"})";
    p.favorite_pending = R"({"favorite":true})";
    p.favorites = {"Geiss = desert", "Flexi - colour"};
    EXPECT_EQ(Preferences::decode(p.encode()).encode(), p.encode());
}

TEST(AfterglowPreferences, ClampsOutOfRangeAndRejectsMalformedNumbers)
{
    const auto low = Preferences::decode("brightness=-1\ntext_brightness=0\ntrim_ms=-9999\n"
                                         "preset_seconds=-5\ntrack_info=-4\nfps=29\n");
    EXPECT_EQ(low.brightness, 10);
    EXPECT_EQ(low.text_brightness, 30);
    EXPECT_EQ(low.trim_ms, -2000);
    EXPECT_EQ(low.preset_seconds, 15);
    EXPECT_EQ(low.track_info, 0);
    EXPECT_EQ(low.fps, 60);
    const auto high = Preferences::decode("brightness=200\ntext_brightness=200\ntrim_ms=9999\n"
                                          "preset_seconds=999\ntrack_info=7\nfps=30\n");
    EXPECT_EQ(high.brightness, 100);
    EXPECT_EQ(high.text_brightness, 100);
    EXPECT_EQ(high.trim_ms, 2000);
    EXPECT_EQ(high.preset_seconds, 600);
    EXPECT_EQ(high.track_info, 2);
    EXPECT_EQ(high.fps, 30);
    const auto malformed =
        Preferences::decode("brightness=20garbage\nfps=\ntrim_ms=999999999999\n");
    EXPECT_EQ(malformed.encode(), Preferences{}.encode());
}

TEST(AfterglowPreferences, HighRefreshSurvivesSaveAndUnsupportedRatesUseSafeDefault)
{
    Preferences p;
    EXPECT_EQ(p.fps, 60);
    p.fps = 120;
    EXPECT_EQ(Preferences::decode(p.encode()).fps, 120);
    EXPECT_EQ(Preferences::decode("fps=30\n").fps, 30);
    EXPECT_EQ(Preferences::decode("fps=60\n").fps, 60);
    for (int unsupported : {-1, 0, 90, 119, 144, 240})
        EXPECT_EQ(Preferences::decode("fps=" + std::to_string(unsupported) + "\n").fps, 60);
}

TEST(AfterglowPreferences, LyricsLayerIsOptionalAndTimingOffsetIsBounded)
{
    Preferences p;
    EXPECT_EQ(p.lyrics_layer, 0);
    EXPECT_EQ(p.lyrics_offset_ms, 0);
    EXPECT_EQ(p.display_mode, 1);
    EXPECT_EQ(p.track_info, 1) << "the track card shows at track changes, not permanently";
    EXPECT_TRUE(p.poster_visuals);
    p.display_mode = 3;
    p.lyrics_layer = 2;
    p.lyrics_offset_ms = -350;
    p.visual_style = 2;
    p.reduce_motion = true;
    p.poster_visuals = false;
    const auto restored = Preferences::decode(p.encode());
    EXPECT_EQ(restored.lyrics_layer, 2);
    EXPECT_EQ(restored.lyrics_offset_ms, -350);
    EXPECT_EQ(restored.display_mode, 3);
    EXPECT_EQ(restored.visual_style, 2);
    EXPECT_TRUE(restored.reduce_motion);
    EXPECT_FALSE(restored.poster_visuals);
    EXPECT_EQ(Preferences::decode("display_mode=-1\n").display_mode, 0);
    EXPECT_EQ(Preferences::decode("display_mode=99\n").display_mode, 3);
    EXPECT_EQ(Preferences::decode("lyrics_layer=7\n").lyrics_layer, 2);
    EXPECT_EQ(Preferences::decode("visual_style=-3\n").visual_style, 0);
    // LG's range: up to 10 s earlier or later.
    EXPECT_EQ(Preferences::decode("lyrics_offset_ms=-9900\n").lyrics_offset_ms, -9900);
    EXPECT_EQ(Preferences::decode("lyrics_offset_ms=-99999\n").lyrics_offset_ms, -10000);
    EXPECT_EQ(Preferences::decode("lyrics_offset_ms=99999\n").lyrics_offset_ms, 10000);
    EXPECT_EQ(Preferences::decode("lyrics_offset_ms=nan\n").lyrics_offset_ms, 0);
}

TEST(AfterglowPreferences, OnlineLyricsIsOnByDefaultSavedAndSynced)
{
    Preferences p;
    EXPECT_TRUE(p.online_lyrics) << "LG: Online lyrics is on by default";
    p.online_lyrics = false;
    EXPECT_FALSE(Preferences::decode(p.encode()).online_lyrics);
    EXPECT_NE(p.encode_synced().find("\"online_lyrics\":0"), std::string::npos);
    EXPECT_FALSE(Preferences{}.with_synced("{\"online_lyrics\":false}").online_lyrics);
    EXPECT_TRUE(p.with_synced("{\"online_lyrics\":1}").online_lyrics);
}

TEST(AfterglowPreferences, FavoriteDuplicatesAndLineEndingsRemainStable)
{
    auto p = Preferences::decode("favorite=one\r\nfavorite=one\nfavorite=two\nplayer=room\r\n"
                                 "favorite=\nfavorite=bad\rname\n");
    EXPECT_EQ(p.favorites, (std::vector<std::string>{"one", "two"}));
    EXPECT_EQ(p.player, "room");
    p.toggle_favorite("one");
    EXPECT_FALSE(p.favorite("one"));
    p.toggle_favorite("one");
    EXPECT_TRUE(p.favorite("one"));
    p.toggle_favorite("");
    p.toggle_favorite("bad\nname");
    EXPECT_EQ(p.favorites.size(), 2U);
}

TEST(AfterglowPreferences, OversizedValuesCannotReplaceValidChoices)
{
    auto p = Preferences::decode("player=room\nplayer=" + std::string(257, 'x') +
                                 "\nfavorite=" + std::string(513, 'x') + "\n");
    EXPECT_EQ(p.player, "room");
    EXPECT_TRUE(p.favorites.empty());
    p.player = "injected\nfollow=1";
    p.favorites = {"injected\nbrightness=10"};
    EXPECT_FALSE(Preferences::decode(p.encode()).follow);
    EXPECT_EQ(Preferences::decode(p.encode()).brightness, 100);
}

TEST(AfterglowPreferenceStore, StopFlushesLatestPayloadInCheckedContainer)
{
    char path[] = "/tmp/afterglow-preferences-XXXXXX";
    const int fd = mkstemp(path);
    ASSERT_GE(fd, 0);
    close(fd);
    hui::afterglow::PreferenceStore store;
    ASSERT_TRUE(store.start(path));
    store.submit("brightness=90\n");
    store.submit("brightness=50\nplayer=kitchen\n");
    store.stop();
    store.join();
    std::string bytes;
    const bool read = hui::save::read_file(path, &bytes);
    unlink(path);
    ASSERT_TRUE(read);
    const auto decoded = hui::save::decode(hui::save::Kind::prefs, bytes);
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.version, 1);
    EXPECT_EQ(decoded.payload, "brightness=50\nplayer=kitchen\n");
    EXPECT_EQ(store.status(), "Preferences saved");
}

TEST(AfterglowPreferenceStore, FailedWriteIsReportedAndShutdownCompletes)
{
    hui::afterglow::PreferenceStore store;
    ASSERT_TRUE(store.start("/dev/null/afterglow.prefs"));
    store.submit("brightness=50\n");
    store.stop();
    store.join();
    EXPECT_EQ(store.status().find("Preferences not saved:"), 0U);
}
