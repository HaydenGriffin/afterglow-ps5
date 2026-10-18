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
    EXPECT_EQ(Preferences::decode("visual_style=3\n").visual_style, hui::afterglow::kVisualOff);
    EXPECT_EQ(Preferences::decode("visual_style=9\n").visual_style, hui::afterglow::kVisualOff);
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

TEST(AfterglowPreferences, LyricOverlayLooksDefaultSaveApartAndSync)
{
    Preferences p;
    EXPECT_EQ(p.line_look, (hui::afterglow::OverlayLook{2, 0, 1})) << "Bottom, Box, Medium";
    EXPECT_EQ(p.stage_look, (hui::afterglow::OverlayLook{1, 0, 1})) << "Middle, Box, Medium";
    p.line_look = {0, 2, 0};
    p.stage_look = {2, 1, 2};
    const auto restored = Preferences::decode(p.encode());
    EXPECT_EQ(restored.line_look, p.line_look);
    EXPECT_EQ(restored.stage_look, p.stage_look);
    const std::string synced = p.encode_synced();
    for (const char *pair : {"\"line_position\":0", "\"line_background\":2", "\"line_size\":0",
                             "\"stage_position\":2", "\"stage_background\":1", "\"stage_size\":2"})
        EXPECT_NE(synced.find(pair), std::string::npos) << pair;
    const auto mirrored = Preferences{}.with_synced(synced);
    EXPECT_EQ(mirrored.line_look, p.line_look);
    EXPECT_EQ(mirrored.stage_look, p.stage_look);
}

TEST(AfterglowPreferences, UnknownOverlayValuesFallBackToEachOverlaysDefault)
{
    const auto p = Preferences::decode("line_position=3\nline_background=-1\nline_size=9\n"
                                       "stage_position=7\nstage_background=x\nstage_size=-2\n");
    EXPECT_EQ(p.line_look, hui::afterglow::kLineLookDefault);
    EXPECT_EQ(p.stage_look, hui::afterglow::kStageLookDefault);
    // One bad value leaves the overlay's other choices alone.
    const auto mixed = Preferences::decode("stage_position=0\nstage_size=5\n");
    EXPECT_EQ(mixed.stage_look, (hui::afterglow::OverlayLook{0, 0, 1}));
    EXPECT_EQ(Preferences{}.with_synced("{\"line_position\":12}").line_look.position, 2);
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

TEST(AfterglowPreferences, FavouritesAreBoundedWhenAddedNotOnlyWhenRead)
{
    Preferences p;
    for (std::size_t i = 0; i < hui::afterglow::kMaxFavorites; ++i)
        ASSERT_TRUE(p.toggle_favorite("Preset " + std::to_string(i)));
    EXPECT_FALSE(p.toggle_favorite("One too many")) << "the next start would not read it";
    EXPECT_FALSE(p.favorite("One too many"));
    EXPECT_TRUE(p.toggle_favorite("Preset 0")) << "removing always works";
    EXPECT_FALSE(p.toggle_favorite(std::string(hui::afterglow::kMaxFavoriteName + 1, 'x')));
    // Favourites set some other way are written only as far as they read back.
    p.favorites.push_back(std::string(hui::afterglow::kMaxFavoriteName + 1, 'x'));
    for (int i = 0; i < 5; ++i)
        p.favorites.push_back("Extra " + std::to_string(i));
    const auto restored = Preferences::decode(p.encode());
    EXPECT_EQ(restored.favorites.size(), hui::afterglow::kMaxFavorites);
    EXPECT_EQ(Preferences::decode(restored.encode()).favorites, restored.favorites);
}

TEST(AfterglowPreferences, TheLargestFileStillReadsBack)
{
    using hui::afterglow::kMaxPreferenceDocument;
    Preferences p;
    p.quality = std::string(kMaxPreferenceDocument, 'q');
    p.favorite_pending = std::string(kMaxPreferenceDocument, 'f');
    p.player = std::string(256, 'p');
    for (std::size_t i = 0; i < hui::afterglow::kMaxFavorites; ++i)
    {
        auto name = std::to_string(i) + " ";
        name.resize(hui::afterglow::kMaxFavoriteName, 'n');
        p.favorites.push_back(name);
    }
    for (std::size_t i = 0; i < hui::afterglow::kSongLimit; ++i)
    {
        hui::afterglow::SongEntry song;
        song.id = std::to_string(i) + ":";
        song.id.resize(hui::afterglow::kSongIdMax, 'i');
        song.sum = "0123abcd";
        song.ms = -10000;
        song.lrclib = 9000000000000000000;
        song.at = 9000000000000000000;
        p.songs.push_back(song);
    }
    const auto file = hui::save::encode(hui::save::Kind::prefs, 1, p.encode());
    EXPECT_LE(file.size(), hui::afterglow::kMaxPreferencesFile);
    const auto restored =
        Preferences::decode(hui::save::decode(hui::save::Kind::prefs, file).payload);
    EXPECT_EQ(restored.encode(), p.encode());
    // One byte more and the value is not written at all, rather than lost on reading.
    p.quality += "q";
    EXPECT_EQ(p.encode().find("quality="), std::string::npos);
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

TEST(AfterglowPreferenceStore, ACheckpointIsWrittenAtOnceAndOrdinaryChangesWait)
{
    char path[] = "/tmp/afterglow-preferences-XXXXXX";
    const int fd = mkstemp(path);
    ASSERT_GE(fd, 0);
    close(fd);
    // A clock that never moves: the half-second debounce never passes.
    hui::afterglow::PreferenceStore store([] { return std::int64_t{1}; });
    ASSERT_TRUE(store.start(path));
    const auto saved_within = [&](int milliseconds)
    {
        for (int waited = 0; waited < milliseconds; waited += 10)
        {
            if (store.status() == "Preferences saved")
                return true;
            usleep(10000);
        }
        return store.status() == "Preferences saved";
    };
    store.submit("brightness=50\n");
    EXPECT_FALSE(saved_within(300)) << "an ordinary change waits for the debounce";
    store.submit_now("brightness=50\nquality={\"pending\":\"P\"}\n");
    EXPECT_TRUE(saved_within(5000)) << "a checkpoint does not";
    std::string bytes;
    ASSERT_TRUE(hui::save::read_file(path, &bytes));
    EXPECT_EQ(hui::save::decode(hui::save::Kind::prefs, bytes).payload,
              "brightness=50\nquality={\"pending\":\"P\"}\n");
    store.submit("brightness=60\n");
    EXPECT_FALSE(saved_within(300)) << "the next ordinary change waits again";
    store.stop();
    store.join();
    unlink(path);
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
