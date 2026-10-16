// Afterglow - The generated stand-in for MilkDrop image textures.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/cloud_texture.hpp"
#include <algorithm>
#include <cstdlib>
#include <gtest/gtest.h>

using hui::afterglow::cloud_texture;
using hui::afterglow::kCloudTextureSize;

namespace
{
int grey(const std::vector<std::uint8_t> &pixels, int x, int y)
{
    return pixels[static_cast<std::size_t>((y * kCloudTextureSize + x) * 4)];
}
} // namespace

TEST(AfterglowCloudTexture, OpaqueGreyRgbaOfTheAdvertisedSize)
{
    const auto pixels = cloud_texture();
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(kCloudTextureSize * kCloudTextureSize * 4));
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        ASSERT_EQ(pixels[i], pixels[i + 1]);
        ASSERT_EQ(pixels[i], pixels[i + 2]);
        ASSERT_EQ(pixels[i + 3], 255);
    }
}

TEST(AfterglowCloudTexture, SamePixelsEveryCall)
{
    EXPECT_EQ(cloud_texture(), cloud_texture());
}

TEST(AfterglowCloudTexture, SpansTheFullRangeWithoutBeingNoise)
{
    const auto pixels = cloud_texture();
    int lowest = 255, highest = 0, steepest = 0;
    long mid_tones = 0;
    for (int y = 0; y < kCloudTextureSize; ++y)
        for (int x = 0; x < kCloudTextureSize; ++x)
        {
            const int value = grey(pixels, x, y);
            lowest = std::min(lowest, value);
            highest = std::max(highest, value);
            mid_tones += value > 64 && value < 192 ? 1 : 0;
            if (x > 0)
                steepest = std::max(steepest, std::abs(value - grey(pixels, x - 1, y)));
        }
    EXPECT_EQ(lowest, 0);
    EXPECT_EQ(highest, 255);
    // Clouds: mostly soft mid tones, no pixel-to-pixel jumps of white noise.
    EXPECT_GT(mid_tones, kCloudTextureSize * kCloudTextureSize / 2);
    EXPECT_LT(steepest, 64);
}

// Presets sample it with wrapping, so the last column runs on into the first
// as smoothly as any two neighbours inside, and the same for rows.
TEST(AfterglowCloudTexture, TilesWithoutASeam)
{
    const auto pixels = cloud_texture();
    const int last = kCloudTextureSize - 1;
    int inside = 0, seam = 0;
    for (int i = 0; i < kCloudTextureSize; ++i)
        for (int j = 1; j < kCloudTextureSize; ++j)
            inside = std::max({inside, std::abs(grey(pixels, j, i) - grey(pixels, j - 1, i)),
                               std::abs(grey(pixels, i, j) - grey(pixels, i, j - 1))});
    for (int i = 0; i < kCloudTextureSize; ++i)
        seam = std::max({seam, std::abs(grey(pixels, 0, i) - grey(pixels, last, i)),
                         std::abs(grey(pixels, i, 0) - grey(pixels, i, last))});
    EXPECT_LE(seam, inside);
}
