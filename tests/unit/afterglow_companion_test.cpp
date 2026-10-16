// Afterglow - Music Assistant relay and metadata decoding regression tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include "afterglow/ma_socket.hpp"
#include <cstdio>
#include <gtest/gtest.h>
#include <limits>
#include <string>

using namespace hui::afterglow;

TEST(AfterglowCompanion, BrowseCommandsMatchLibraryAndAlbumApi)
{
    BrowseRequest request;
    auto wire = companion_wire::browse_request(request);
    EXPECT_EQ(wire.command, "music/recently_played_items");
    EXPECT_EQ(Json(wire.args).integer(Json(wire.args).get(0, "limit")), 31);
    request.category = BrowseCategory::favorite_tracks;
    wire = companion_wire::browse_request(request);
    EXPECT_EQ(wire.command, "music/tracks/library_items");
    Json args(wire.args);
    EXPECT_TRUE(args.boolean(args.get(0, "favorite")));
    EXPECT_EQ(args.string(args.get(0, "order_by")), "timestamp_added_desc");
    request.category = BrowseCategory::tracks;
    request.item_id = "album\"id";
    request.media_type = "album";
    wire = companion_wire::browse_request(request);
    EXPECT_EQ(wire.command, "music/albums/album_tracks");
    Json album(wire.args);
    EXPECT_EQ(album.string(album.get(0, "item_id")), "album\"id");
    EXPECT_EQ(album.string(album.get(0, "provider_instance_id_or_domain")), "library");
    request.media_type = "track";
    EXPECT_FALSE(companion_wire::browse_request(request).error.empty());
}

TEST(AfterglowCompanion, QueueActionsUseCapturedTargetAndClampSeek)
{
    CompanionSnapshot target;
    target.connected = true;
    target.queue_id = "room\"queue";
    target.track.duration = 180;
    CompanionAction action;
    action.kind = CompanionActionKind::seek;
    action.value = 999;
    const auto wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.command, "player_queues/seek");
    Json args(wire.args);
    EXPECT_EQ(args.string(args.get(0, "queue_id")), "room\"queue");
    EXPECT_EQ(args.integer(args.get(0, "position")), 180);
    target.track.duration = 0;
    EXPECT_FALSE(companion_wire::action_request(action, target).error.empty());
    target.connected = false;
    action.kind = CompanionActionKind::play_media;
    action.text = "library://track/1";
    action.option = "replace";
    EXPECT_FALSE(companion_wire::action_request(action, target).error.empty());
}

TEST(AfterglowCompanion, PlayAndRadioPreserveMediaUrisAndQueueOptions)
{
    CompanionSnapshot target;
    target.connected = true;
    target.queue_id = "room";
    target.track.uri = "spotify://track/current";
    CompanionAction action;
    action.kind = CompanionActionKind::play_media;
    action.text = "library://playlist/3";
    for (const char *option : {"replace", "next", "add"})
    {
        action.option = option;
        const auto wire = companion_wire::action_request(action, target);
        EXPECT_TRUE(wire.error.empty());
        EXPECT_EQ(wire.command, "player_queues/play_media");
        Json args(wire.args);
        EXPECT_EQ(args.string(args.get(0, "media")), action.text);
        EXPECT_EQ(args.string(args.get(0, "option")), option);
    }
    action.option = "invalid";
    EXPECT_FALSE(companion_wire::action_request(action, target).error.empty());
    action.kind = CompanionActionKind::radio;
    action.text.clear();
    const auto wire = companion_wire::action_request(action, target);
    Json args(wire.args);
    EXPECT_EQ(args.string(args.get(0, "media")),
              "radio_playlist://playlist/spotify://track/current");
    EXPECT_EQ(args.string(args.get(0, "option")), "replace");
}

TEST(AfterglowCompanion, QueueItemEditsUseStableIds)
{
    CompanionSnapshot target;
    target.connected = true;
    target.queue_id = "queue";
    CompanionAction action;
    action.text = "queue-item-42";
    action.kind = CompanionActionKind::play_index;
    auto wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.command, "player_queues/play_index");
    EXPECT_EQ(Json(wire.args).string(Json(wire.args).get(0, "index")), action.text);
    action.kind = CompanionActionKind::delete_item;
    wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.command, "player_queues/delete_item");
    EXPECT_EQ(Json(wire.args).string(Json(wire.args).get(0, "item_id_or_index")), action.text);
    action.kind = CompanionActionKind::move_item;
    action.value = -1;
    wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.command, "player_queues/move_item");
    EXPECT_EQ(Json(wire.args).integer(Json(wire.args).get(0, "pos_shift")), -1);
}

TEST(AfterglowCompanion, PlaybackOptionsUseExplicitDesiredValues)
{
    CompanionSnapshot target;
    target.connected = true;
    target.queue_id = "queue";
    target.player_id = "player";
    target.track.uri = "library://track/1";
    CompanionAction action;
    action.enabled = true;
    action.kind = CompanionActionKind::shuffle;
    auto wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.command, "player_queues/shuffle");
    EXPECT_TRUE(Json(wire.args).boolean(Json(wire.args).get(0, "shuffle_enabled")));
    action.kind = CompanionActionKind::repeat;
    action.text = "one";
    EXPECT_TRUE(companion_wire::action_request(action, target).error.empty());
    action.text = "invalid";
    EXPECT_FALSE(companion_wire::action_request(action, target).error.empty());
    action.kind = CompanionActionKind::sleep;
    action.value = 30;
    wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.command, "players/sleep_timer/set");
    EXPECT_EQ(Json(wire.args).string(Json(wire.args).get(0, "player_id")), "queue");
    EXPECT_EQ(Json(wire.args).integer(Json(wire.args).get(0, "seconds")), 1800);
    action.value = 0;
    EXPECT_EQ(companion_wire::action_request(action, target).command, "players/sleep_timer/clear");
    action.kind = CompanionActionKind::favorite;
    action.text.clear();
    wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.command, "music/favorites/add_item");
    EXPECT_EQ(Json(wire.args).string(Json(wire.args).get(0, "item")), target.track.uri);
    action.enabled = false;
    wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.command, "music/favorites/remove_item");
    EXPECT_EQ(Json(wire.args).string(Json(wire.args).get(0, "library_item_id")), "1");
    target.track.uri = "spotify://track/abc";
    target.track.item_id = "abc";
    target.track.provider = "spotify";
    target.track.media_type = "track";
    wire = companion_wire::action_request(action, target);
    EXPECT_EQ(wire.command, "music/get_library_item");
    EXPECT_EQ(Json(wire.args).string(Json(wire.args).get(0, "item_id")), "abc");
}

TEST(AfterglowCompanion, BrowseDecodeRetainsStableIdsAndFormatWithinLimit)
{
    const auto items = companion_wire::decode_items(R"({"items":[
        {"queue_item_id":"q1","media_item":{"uri":"library://track/1","name":"One","item_id":"1","provider":"library","media_type":"track","favorite":true},"streamdetails":{"audio_format":{"content_type":"flac","sample_rate":96000,"bit_depth":24}}},
        {"uri":"library://track/2","name":"Two"}]})",
                                                    1);
    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items[0].queue_item_id, "q1");
    EXPECT_EQ(items[0].provider, "library");
    EXPECT_EQ(items[0].item_id, "1");
    EXPECT_EQ(items[0].media_type, "track");
    EXPECT_TRUE(items[0].favorite);
    EXPECT_EQ(items[0].format, "flac / 96000 Hz / 24 bit");
    EXPECT_TRUE(companion_wire::decode_items("[]", 0).empty());
}

TEST(AfterglowCompanion, WaveUsesBigEndianTimestampAndUnsignedSilence)
{
    std::array<std::uint8_t, 1033> bytes{};
    bytes.fill(128);
    bytes[0] = 22;
    constexpr std::int64_t timestamp = 1234567890123;
    for (int i = 0; i < 8; ++i)
        bytes[static_cast<std::size_t>(i + 1)] =
            static_cast<std::uint8_t>(timestamp >> ((7 - i) * 8));
    bytes[9] = 0;
    bytes[1032] = 255;
    companion_wire::Wave wave;
    ASSERT_TRUE(companion_wire::decode_wave(bytes.data(), bytes.size(), wave));
    EXPECT_EQ(wave.time, timestamp);
    EXPECT_EQ(wave.samples.front(), 0);
    EXPECT_EQ(wave.samples[1], 128);
    EXPECT_EQ(wave.samples.back(), 255);
    EXPECT_FALSE(companion_wire::decode_wave(bytes.data(), bytes.size() - 1, wave));
    bytes[0] = 17;
    EXPECT_FALSE(companion_wire::decode_wave(bytes.data(), bytes.size(), wave));
    bytes[0] = 22;
    bytes[1] = 255;
    EXPECT_FALSE(companion_wire::decode_wave(bytes.data(), bytes.size(), wave));
    EXPECT_FALSE(companion_wire::decode_wave(nullptr, bytes.size(), wave));
}

TEST(AfterglowCompanion, ReadsNestedMetadataWithoutConfusingRepeatedNames)
{
    auto track = companion_wire::decode_track(
        R"({"name":"fallback","duration":123,"media_item":{"uri":"library://track/1","name":"Miracle","artists":[{"name":"Blackmill"},{"name":"Guest"}],"album":{"name":"Album","year":2011},"metadata":{"label":"Label","lyrics":"First\nSecond","lrc_lyrics":"[00:01.00]First","images":[{"path":"wrong","proxy_id":"abc"}]}}})");
    EXPECT_EQ(track.title, "Miracle");
    EXPECT_EQ(track.artist, "Blackmill, Guest");
    EXPECT_EQ(track.album, "Album");
    EXPECT_EQ(track.year, "2011");
    EXPECT_EQ(track.label, "Label");
    EXPECT_EQ(track.duration, 123);
    EXPECT_EQ(track.artwork, "/imageproxy/abc?size=512&fmt=png");
    EXPECT_EQ(track.lyrics, "First\nSecond");
    EXPECT_EQ(track.lrc_lyrics, "[00:01.00]First");
}

TEST(AfterglowCompanion, ReadsLongPlainAndEnhancedLyricsWhole)
{
    std::string plain, plain_json, lrc, lrc_json;
    for (int i = 0; i < 200; ++i)
    {
        const auto line = "Line " + std::to_string(i) + " of a long fixture song";
        plain += line + "\n";
        plain_json += line + "\\n";
        char stamp[32];
        std::snprintf(stamp, sizeof(stamp), "[%02d:%02d.00]", i / 60, i % 60);
        const auto timed = std::string(stamp) + "<00:00.10> Word <00:00.40> \\u00e9t\\u00e9";
        lrc += std::string(stamp) + "<00:00.10> Word <00:00.40> été\n";
        lrc_json += timed + "\\n";
    }
    ASSERT_GT(plain.size(), 5000U);
    ASSERT_GT(lrc.size(), 5000U);
    const auto track = companion_wire::decode_track(
        R"({"name":"x","media_item":{"uri":"library://track/9","name":"Long","metadata":{"lyrics":")" +
        plain_json + R"(","lrc_lyrics":")" + lrc_json + R"("}}})");
    EXPECT_EQ(track.lyrics, plain);
    EXPECT_EQ(track.lrc_lyrics, lrc);
}

TEST(AfterglowCompanion, RelayClockRefusesValuesThatWouldOverflow)
{
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();
    const std::int64_t now = 5000000000; // 5000 s of uptime
    companion_wire::RelayClock clock;
    // A sane reply: sent 40 ms ago, the server 1 s ahead of the midpoint.
    ASSERT_TRUE(companion_wire::relay_clock(now / 1000 - 40, now - 20000 + 1000000, now, clock));
    EXPECT_EQ(clock.rtt_us, 40000);
    EXPECT_EQ(clock.offset_us, 1000000);
    for (const std::int64_t client : {max, min, max / 1000, std::int64_t{-1}, now / 1000 + 1})
    {
        clock = {};
        EXPECT_FALSE(companion_wire::relay_clock(client, now, now, clock)) << client;
        EXPECT_EQ(clock.offset_us, 0) << client;
    }
    for (const std::int64_t server :
         {max, min, std::int64_t{0}, std::int64_t{-1}, companion_wire::kMaxRelayClockUs + 1})
        EXPECT_FALSE(companion_wire::relay_clock(now / 1000, server, now, clock)) << server;
    for (const std::int64_t at : {min, std::int64_t{-1}, max})
        EXPECT_FALSE(companion_wire::relay_clock(0, 1, at, clock)) << at;
    // Stale: answered more than 10 s after it was sent.
    EXPECT_FALSE(companion_wire::relay_clock(now / 1000 - 10001, now, now, clock));
    EXPECT_TRUE(companion_wire::relay_clock(now / 1000 - 10000, now, now, clock));
    // The extremes still accepted give an offset the monotonic clock can take.
    ASSERT_TRUE(
        companion_wire::relay_clock(now / 1000, companion_wire::kMaxRelayClockUs, now, clock));
    EXPECT_LE(clock.offset_us, companion_wire::kMaxRelayClockUs);
    ASSERT_TRUE(companion_wire::relay_clock(now / 1000, 1, now, clock));
    EXPECT_GE(clock.offset_us, -now);
}

TEST(AfterglowCompanion, HandlesRadioAndUnavailableMetadata)
{
    auto track = companion_wire::decode_track(R"({"name":"Blackmill - Spirit","media_item":null})");
    EXPECT_EQ(track.artist, "Blackmill");
    EXPECT_EQ(track.title, "Spirit");
    EXPECT_TRUE(track.artwork.empty());
    EXPECT_TRUE(track.lyrics.empty());
    EXPECT_TRUE(companion_wire::decode_track("null").title.empty());
}

TEST(AfterglowCompanion, RejectsInvalidConfigurationWithoutStartingAWorker)
{
    Companion companion;
    EXPECT_FALSE(companion.start("localhost", 8095, "test"));
    EXPECT_FALSE(companion.start("127.0.0.1", 8095, ""));
    EXPECT_FALSE(companion.start("127.0.0.1", 0, "test"));
    EXPECT_FALSE(companion.snapshot().connected);
    EXPECT_TRUE(companion.take_pcm().empty());
    companion.stop();
    companion.join();
}

TEST(AfterglowCompanion, FavoritesMergePreservesOtherDevicesAndNewerChanges)
{
    const auto merged = companion_wire::merge_favorites(
        R"({"unrelated":{"secret":"preserve"},"visualizer_favorites":["LG"],"vizman":{"devices":{"tv":{"dim":40}},"mirrored":["LG"],"favoritesMeta":{"LG":{"on":true,"at":200},"Removed":{"on":false,"at":300}}}})",
        {{"LG", {false, 100}}, {"PS5", {true, 400}}, {"Removed", {true, 200}}}, 500);
    Json json(merged);
    const auto favorites = json.get(0, "visualizer_favorites");
    EXPECT_TRUE(json.has_string(favorites, "LG"));
    EXPECT_TRUE(json.has_string(favorites, "PS5"));
    EXPECT_FALSE(json.has_string(favorites, "Removed"));
    EXPECT_EQ(json.string(json.get(json.get(0, "unrelated"), "secret")), "preserve");
    const auto devices = json.get(json.get(0, "vizman"), "devices");
    EXPECT_EQ(json.integer(json.get(json.get(devices, "tv"), "dim")), 40);
}

TEST(AfterglowCompanion, FavoritesMergeRecognizesChangesMadeInMusicAssistant)
{
    const auto merged = companion_wire::merge_favorites(
        R"({"visualizer_favorites":["New"],"vizman":{"mirrored":["Old"],"favoritesMeta":{"Old":{"on":true,"at":200}}}})",
        {}, 500);
    Json json(merged);
    const auto favorites = json.get(0, "visualizer_favorites");
    EXPECT_TRUE(json.has_string(favorites, "New"));
    EXPECT_FALSE(json.has_string(favorites, "Old"));
    const auto old = json.get(json.get(json.get(0, "vizman"), "favoritesMeta"), "Old");
    EXPECT_EQ(json.integer(json.get(old, "at")), 500);
}

TEST(AfterglowCompanion, DeviceOnlySaveLeavesFavouritesAsWritten)
{
    // Unsorted, as another client may have written them; merging would sort.
    constexpr const char *kStored =
        R"({"visualizer_favorites":["Zed","Alpha"],"vizman":{"mirrored":["Zed","Alpha"],"favoritesMeta":{"Zed":{"on":true,"at":1},"Alpha":{"on":true,"at":2}}}})";
    EXPECT_TRUE(companion_wire::account_save(kStored, {}, "ps5-x", "", 500).empty())
        << "nothing changed: nothing to save";
    const auto device = companion_wire::account_save(kStored, {}, "ps5-x", R"({"dim":3})", 500);
    EXPECT_NE(device.find(R"("visualizer_favorites":["Zed","Alpha"])"), std::string::npos)
        << device;
    Json json(device);
    const auto devices = json.get(json.get(0, "vizman"), "devices");
    EXPECT_EQ(json.integer(json.get(json.get(devices, "ps5-x"), "dim")), 3);
    const auto merged =
        companion_wire::account_save(kStored, {{"New", {true, 600}}}, "ps5-x", "", 700);
    EXPECT_TRUE(Json(merged).has_string(Json(merged).get(0, "visualizer_favorites"), "New"));
}

TEST(AfterglowCompanion, DownsamplesHighResolutionRelayFrames)
{
    for (const int rate : {44100, 48000, 88200, 96000, 176400, 192000})
    {
        companion_wire::WaveResampler resampler;
        companion_wire::Wave frame;
        frame.time = 1000000;
        frame.samples.fill(192);
        std::vector<float> pcm;
        resampler.append(frame, pcm);
        pcm.clear();
        frame.time += 1024000000 / rate;
        resampler.append(frame, pcm);
        EXPECT_EQ(resampler.source_rate(), rate);
        const int factor = rate >= 176400 ? 4 : rate >= 88200 ? 2 : 1;
        ASSERT_EQ(pcm.size(), 2048U / static_cast<std::size_t>(factor));
        for (float sample : pcm)
            EXPECT_FLOAT_EQ(sample, 0.5f);
        resampler.reset();
        EXPECT_EQ(resampler.source_rate(), 48000);
    }
}

TEST(AfterglowCompanion, DownsamplingAveragesRatherThanDiscardingHighFrequencySamples)
{
    companion_wire::WaveResampler resampler;
    companion_wire::Wave frame;
    frame.time = 1000000;
    for (std::size_t i = 0; i < frame.samples.size(); ++i)
        frame.samples[i] = i % 2 ? 192 : 64;
    std::vector<float> pcm;
    resampler.append(frame, pcm);
    pcm.clear();
    frame.time += 10667;
    resampler.append(frame, pcm);
    ASSERT_EQ(pcm.size(), 1024U);
    for (float sample : pcm)
        EXPECT_FLOAT_EQ(sample, 0);
}

TEST(AfterglowCompanion, PendingFavoriteJournalSurvivesRestartAndPreservesNewerEdits)
{
    Companion first;
    first.set_favorite("Preset", true);
    const auto journal = first.pending_favorites();
    EXPECT_EQ(journal.find('\n'), std::string::npos);
    Companion second;
    second.restore_pending_favorites(journal);
    EXPECT_EQ(second.pending_favorites(), journal);
    second.restore_pending_favorites(
        R"({"Preset":{"on":false,"at":1},"Other":{"on":false,"at":2}})");
    // Json borrows its input: hold the journal while inspecting its tokens.
    const auto stored = second.pending_favorites();
    Json values(stored);
    EXPECT_TRUE(values.boolean(values.get(values.get(0, "Preset"), "on")));
    EXPECT_FALSE(values.boolean(values.get(values.get(0, "Other"), "on")));
    second.restore_pending_favorites(R"({"Invalid":{"on":"true","at":5}})");
    EXPECT_EQ(second.pending_favorites().find("Invalid"), std::string::npos);
}

TEST(AfterglowCompanion, LargeAccountSavesUseA64BitFrameUpToTheCap)
{
    namespace ws = hui::afterglow::ma_socket;
    const std::array<std::uint8_t, 4> mask{1, 2, 3, 4};
    std::vector<std::uint8_t> text(70000, 'a');
    const auto frame = ws::encode_frame(1, text.data(), text.size(), mask);
    ASSERT_EQ(frame.size(), 2u + 8u + 4u + text.size());
    EXPECT_EQ(frame[0], 0x81);
    EXPECT_EQ(frame[1], 0xff) << "masked, 64-bit length";
    std::uint64_t length = 0;
    for (int i = 2; i < 10; ++i)
        length = (length << 8) | frame[static_cast<std::size_t>(i)];
    EXPECT_EQ(length, text.size());
    EXPECT_EQ(frame[14] ^ mask[0], 'a');
    EXPECT_EQ(frame.back() ^ mask[(text.size() - 1) % 4], 'a');
    const auto medium = ws::encode_frame(1, text.data(), 300, mask);
    EXPECT_EQ(medium[1], 0xfe);
    EXPECT_EQ(medium.size(), 2u + 2u + 4u + 300u);
    text.resize(ws::kMaxOutgoing + 1);
    EXPECT_TRUE(ws::encode_frame(1, text.data(), text.size(), mask).empty());
    static_assert(companion_wire::kMaxAccountSave < ws::kMaxOutgoing);
}
