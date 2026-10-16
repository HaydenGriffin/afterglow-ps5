// Afterglow - Tests for the cover picture MilkDrop presets sample and its fade schedule.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/cover_texture.hpp"
#include <gtest/gtest.h>

using namespace hui::afterglow;

namespace
{
constexpr std::size_t kBytes = static_cast<std::size_t>(kCoverSize) * kCoverSize * 4;

std::uint8_t red_at(const std::vector<std::uint8_t> &pixels, int x, int y)
{
    return pixels[static_cast<std::size_t>((y * kCoverSize + x) * 4)];
}
} // namespace

TEST(AfterglowCoverTexture, MiddleSquareScaledWithTheTopRowFirst)
{
    // 300 x 200: red 0 in the left and right 50 columns, the top half of the
    // middle square red 200, the bottom half red 100.
    constexpr int w = 300, h = 200;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w * h * 4), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 50; x < 250; ++x)
            rgba[static_cast<std::size_t>((y * w + x) * 4)] = y < 100 ? 200 : 100;
    const auto pixels = cover_pixels(rgba, w, h);
    ASSERT_EQ(pixels.size(), kBytes);
    EXPECT_EQ(red_at(pixels, kCoverSize / 2, 0), 200) << "the picture's top is the first row";
    EXPECT_EQ(red_at(pixels, kCoverSize / 2, kCoverSize - 1), 100);
    EXPECT_EQ(red_at(pixels, 2, 2), 200) << "the side columns are cropped away";
    EXPECT_EQ(pixels[3], 255);
    EXPECT_TRUE(cover_pixels(rgba, w, h + 1).empty());
    EXPECT_TRUE(cover_pixels({}, 0, 0).empty());
}

TEST(AfterglowCoverTexture, CloudsFillTheTextureAndBlendReachesBothEnds)
{
    const auto clouds = cover_clouds();
    ASSERT_EQ(clouds.size(), kBytes);
    const std::vector<std::uint8_t> white(kBytes, 255);
    std::vector<std::uint8_t> out;
    blend_pixels(clouds, white, 0, out);
    EXPECT_EQ(out, clouds);
    blend_pixels(clouds, white, 1, out);
    EXPECT_EQ(out, white);
    blend_pixels(std::vector<std::uint8_t>(kBytes, 0), white, 0.5f, out);
    EXPECT_NEAR(out[0], 128, 1);
}

TEST(AfterglowCoverTexture, ScheduleWaitsToSettleAndKeepsGaps)
{
    CoverSchedule schedule;
    EXPECT_FALSE(schedule.tick(0)) << "the clouds are already shown";
    schedule.want(1, 1000);
    EXPECT_FALSE(schedule.tick(1500));
    schedule.touch(1800); // the colours arrived
    EXPECT_FALSE(schedule.tick(2500));
    const auto fade = schedule.tick(2800);
    ASSERT_TRUE(fade);
    EXPECT_EQ(fade->from, 0u);
    EXPECT_EQ(fade->to, 1u);
    EXPECT_FLOAT_EQ(schedule.level(3800), 0.5f);
    EXPECT_FLOAT_EQ(schedule.level(9000), 1.0f);
    schedule.want(2, 3000);
    schedule.want(3, 3500);
    EXPECT_FALSE(schedule.tick(5000)) << "no second fade within 4 s";
    const auto next = schedule.tick(6800);
    ASSERT_TRUE(next);
    EXPECT_EQ(next->from, 1u);
    EXPECT_EQ(next->to, 3u) << "the newest waiting picture";
    schedule.touch(7000);
    EXPECT_FALSE(schedule.tick(20000)) << "colours alone start nothing";
}
