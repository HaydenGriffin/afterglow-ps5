// Afterglow - The generated cloud texture MilkDrop presets get for images that are not shipped.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/cloud_texture.hpp"
#include <algorithm>
#include <cmath>

namespace hui::afterglow
{
namespace
{
// Value noise: a random value at each lattice point, smoothly interpolated.
// Each octave's lattice wraps at the texture edge, so the sum tiles, as
// MilkDrop's wrapped samplers expect.
constexpr int kOctaves = 5;
constexpr int kFirstCells = 4; // lattice cells across the texture in the coarsest octave

float lattice(int x, int y, int octave)
{
    auto h = static_cast<std::uint32_t>(x) * 0x8da6b343u ^
             static_cast<std::uint32_t>(y) * 0xd8163841u ^
             static_cast<std::uint32_t>(octave + 1) * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffu) / 65535.0f;
}

float smooth(float t)
{
    return t * t * (3 - 2 * t);
}

float octave_value(int x, int y, int octave)
{
    const int cells = kFirstCells << octave;
    const float scale = static_cast<float>(cells) / kCloudTextureSize;
    const float u = static_cast<float>(x) * scale, v = static_cast<float>(y) * scale;
    const int x0 = static_cast<int>(u), y0 = static_cast<int>(v);
    const int x1 = (x0 + 1) % cells, y1 = (y0 + 1) % cells;
    const float fx = smooth(u - static_cast<float>(x0)), fy = smooth(v - static_cast<float>(y0));
    const float top =
        lattice(x0, y0, octave) + (lattice(x1, y0, octave) - lattice(x0, y0, octave)) * fx;
    const float bottom =
        lattice(x0, y1, octave) + (lattice(x1, y1, octave) - lattice(x0, y1, octave)) * fx;
    return top + (bottom - top) * fy;
}
} // namespace

std::vector<std::uint8_t> cloud_texture()
{
    constexpr int kPixels = kCloudTextureSize * kCloudTextureSize;
    std::vector<float> field(kPixels);
    for (int y = 0; y < kCloudTextureSize; ++y)
        for (int x = 0; x < kCloudTextureSize; ++x)
        {
            float value = 0, amplitude = 1;
            for (int octave = 0; octave < kOctaves; ++octave)
            {
                value += octave_value(x, y, octave) * amplitude;
                amplitude *= 0.5f;
            }
            field[y * kCloudTextureSize + x] = value;
        }
    // Stretched to the full range: presets expect a picture, not a flat grey.
    const auto [low, high] = std::minmax_element(field.begin(), field.end());
    const float lowest = *low, range = std::max(*high - *low, 1e-6f);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kPixels) * 4);
    for (int i = 0; i < kPixels; ++i)
    {
        const auto grey = static_cast<std::uint8_t>(std::lround((field[i] - lowest) / range * 255));
        pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = grey;
        pixels[i * 4 + 3] = 255;
    }
    return pixels;
}
} // namespace hui::afterglow
