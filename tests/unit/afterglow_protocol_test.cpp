// Afterglow - PCM scheduling, stream invalidation and malformed-input tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/protocol.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <vector>

namespace hui::afterglow
{
namespace
{
std::vector<std::uint8_t> pcm(std::int64_t timestamp, std::size_t frames, std::int16_t left = 12000,
                              std::int16_t right = -12000)
{
    std::vector<std::uint8_t> bytes(9 + frames * 4);
    bytes[0] = 4;
    for (int i = 0; i < 8; ++i)
        bytes[static_cast<std::size_t>(i + 1)] =
            static_cast<std::uint8_t>(static_cast<std::uint64_t>(timestamp) >> ((7 - i) * 8));
    for (std::size_t i = 0; i < frames; ++i)
    {
        bytes[9 + i * 4] = static_cast<std::uint8_t>(left);
        bytes[10 + i * 4] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(left) >> 8);
        bytes[11 + i * 4] = static_cast<std::uint8_t>(right);
        bytes[12 + i * 4] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(right) >> 8);
    }
    return bytes;
}

TEST(AfterglowAudio, WaitsForServerTimestampAndDecodesLittleEndianStereo)
{
    AudioQueue queue;
    auto bytes = pcm(1000000, 480);
    ASSERT_TRUE(queue.push(bytes.data(), bytes.size()));
    std::array<std::int16_t, 960> out{};
    queue.render(out.data(), 480, 980000, 100, false, true);
    EXPECT_TRUE(std::all_of(out.begin(), out.end(), [](auto v) { return v == 0; }));
    EXPECT_EQ(queue.underruns(), 0U);
    EXPECT_DOUBLE_EQ(queue.buffered_ms(), 10.0);
    queue.render(out.data(), 480, 1000000, 50, false, true);
    for (std::size_t i = 0; i < out.size(); i += 2)
    {
        EXPECT_EQ(out[i], 6000);
        EXPECT_EQ(out[i + 1], -6000);
    }
    EXPECT_DOUBLE_EQ(queue.buffered_ms(), 0);
}

TEST(AfterglowAudio, ClearDiscardsPartiallyConsumedOldGeneration)
{
    AudioQueue queue;
    auto old = pcm(1000000, 480, 100, 100);
    ASSERT_TRUE(queue.push(old.data(), old.size()));
    std::array<std::int16_t, 480> out{};
    queue.render(out.data(), 240, 1000000, 100, false, true);
    queue.clear();
    EXPECT_TRUE(queue.clear_pending());
    auto fresh = pcm(1010000, 240, 200, -200);
    ASSERT_TRUE(queue.push(fresh.data(), fresh.size()));
    queue.render(out.data(), 240, 1010000, 100, false, true);
    EXPECT_FALSE(queue.clear_pending());
    EXPECT_EQ(out[0], 200);
    EXPECT_EQ(out[1], -200);
    EXPECT_DOUBLE_EQ(queue.buffered_ms(), 0);
    EXPECT_EQ(queue.underruns(), 0U);
}

TEST(AfterglowAudio, LateChunksAreDiscardedAndUnderrunsExcludeIdleTime)
{
    AudioQueue queue;
    std::array<std::int16_t, 960> out{};
    queue.render(out.data(), 480, 1000000, 100, false, true);
    EXPECT_EQ(queue.underruns(), 0U);
    auto stale = pcm(900000, 480);
    auto fresh = pcm(1000000, 480);
    ASSERT_TRUE(queue.push(stale.data(), stale.size()));
    ASSERT_TRUE(queue.push(fresh.data(), fresh.size()));
    queue.render(out.data(), 480, 1000000, 100, false, true);
    EXPECT_EQ(queue.late_frames(), 480U);
    EXPECT_EQ(out[0], 12000);
    queue.render(out.data(), 480, 1010000, 100, false, true);
    EXPECT_EQ(queue.underruns(), 1U);
    queue.clear();
    queue.render(out.data(), 480, 1020000, 100, false, false);
    EXPECT_EQ(queue.underruns(), 1U);
}

TEST(AfterglowAudio, BoundsBufferAndRejectsMalformedPackets)
{
    AudioQueue queue;
    EXPECT_FALSE(queue.push(nullptr, 0));
    auto bytes = pcm(1000000, 4800);
    for (std::size_t i = 0; i < kBufferBytes / (4800 * 4); ++i)
        ASSERT_TRUE(queue.push(bytes.data(), bytes.size()));
    EXPECT_FALSE(queue.push(bytes.data(), bytes.size()));
    EXPECT_EQ(queue.overflows(), 1U);
    EXPECT_LE(queue.buffered_ms(), static_cast<double>(kBufferBytes) / 192.0);
    bytes[0] = 3;
    EXPECT_FALSE(queue.push(bytes.data(), bytes.size()));
    bytes[0] = 4;
    EXPECT_FALSE(queue.push(bytes.data(), bytes.size() - 1));
    auto negative_time = pcm(-1, 10);
    EXPECT_FALSE(queue.push(negative_time.data(), negative_time.size()));
}

TEST(AfterglowAudio, SplitsLargePacketsAndMutesWithoutLosingTime)
{
    AudioQueue queue;
    auto bytes = pcm(1000000, 9600);
    ASSERT_TRUE(queue.push(bytes.data(), bytes.size()));
    std::vector<std::int16_t> out(19200);
    queue.render(out.data(), 9600, 1000000, 100, true, true);
    EXPECT_TRUE(std::all_of(out.begin(), out.end(), [](auto v) { return v == 0; }));
    EXPECT_EQ(queue.late_frames(), 0U);
    EXPECT_EQ(queue.underruns(), 0U);
    EXPECT_DOUBLE_EQ(queue.buffered_ms(), 0);
}

TEST(AfterglowAudio, ExtremeFutureTimestampCannotOverflowScheduler)
{
    AudioQueue queue;
    auto bytes = pcm(INT64_MAX / 4, 10);
    ASSERT_TRUE(queue.push(bytes.data(), bytes.size()));
    std::array<std::int16_t, 960> out{};
    queue.render(out.data(), 480, 1000000, 100, false, true);
    EXPECT_TRUE(std::all_of(out.begin(), out.end(), [](auto v) { return v == 0; }));
}

TEST(AfterglowJson, FindsDirectChildrenAndDecodesUnicodeAndEscapes)
{
    Json json(
        R"({"type":"server/state","payload":{"metadata":{"title":"Caf\u00e9 \ud83c\udfb5","artist":"A\nB"},"title":"wrong"}})");
    ASSERT_TRUE(json.valid());
    const int payload = json.get(0, "payload");
    const int metadata = json.get(payload, "metadata");
    EXPECT_EQ(json.string(json.get(metadata, "title")), "Café 🎵");
    EXPECT_EQ(json.string(json.get(metadata, "artist")), "A\nB");
    EXPECT_EQ(json.get(0, "title"), -1);
    EXPECT_EQ(json.string(json.get(payload, "title")), "wrong");
    const std::string encoded = "{\"value\":" + json_quote("A\n\"B\\") + "}";
    Json roundtrip(encoded);
    ASSERT_TRUE(roundtrip.valid());
    EXPECT_EQ(roundtrip.string(roundtrip.get(0, "value")), "A\n\"B\\");
}

TEST(AfterglowJson, RejectsTruncationAndBoundsTokens)
{
    EXPECT_FALSE(Json("{\"payload\":{").valid());
    EXPECT_FALSE(Json("{}{}").valid());
    EXPECT_FALSE(Json("[]").valid());
    EXPECT_FALSE(Json("{\"x\":\"\\u123\"}").valid());
    Json json(R"({"roles":["metadata","player"],"number":9223372036854775808,"ok":true})");
    ASSERT_TRUE(json.valid());
    EXPECT_TRUE(json.has_string(json.get(0, "roles"), "player"));
    EXPECT_FALSE(json.has_string(json.get(0, "roles"), "controller"));
    EXPECT_EQ(json.integer(json.get(0, "number"), 42), 42);
    EXPECT_TRUE(json.boolean(json.get(0, "ok")));
}
} // namespace
} // namespace hui::afterglow
