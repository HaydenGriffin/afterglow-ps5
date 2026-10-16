// Afterglow - The song's cover as the picture MilkDrop presets sample, and when it changes.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/cover_texture.hpp"
#include "afterglow/cloud_texture.hpp"
#include <algorithm>
#include <cmath>

namespace hui::afterglow
{
namespace
{
constexpr double kSettleMs = 1000, kGapMs = 4000, kFadeMs = 2000;
constexpr std::size_t kCoverBytes = static_cast<std::size_t>(kCoverSize) * kCoverSize * 4;

// Bilinear sample of channel c at (x, y) in source pixels; `wrap` tiles,
// otherwise the edge is held.
float sample(const std::vector<std::uint8_t> &rgba, int width, int height, float x, float y, int c,
             bool wrap)
{
    const auto at = [&](int px, int py)
    {
        if (wrap)
        {
            px = ((px % width) + width) % width;
            py = ((py % height) + height) % height;
        }
        else
        {
            px = std::clamp(px, 0, width - 1);
            py = std::clamp(py, 0, height - 1);
        }
        return static_cast<float>(rgba[static_cast<std::size_t>((py * width + px) * 4 + c)]);
    };
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0), fy = y - static_cast<float>(y0);
    const float top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
    const float bottom = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
    return top + (bottom - top) * fy;
}

// A kCoverSize picture from the square at (left, top, side) of the source.
std::vector<std::uint8_t> scaled(const std::vector<std::uint8_t> &rgba, int width, int height,
                                 float left, float top, float side, bool wrap)
{
    std::vector<std::uint8_t> out(kCoverBytes);
    const float step = side / kCoverSize;
    for (int y = 0; y < kCoverSize; ++y)
    {
        const float sy = top + (static_cast<float>(y) + 0.5f) * step - 0.5f;
        for (int x = 0; x < kCoverSize; ++x)
        {
            const float sx = left + (static_cast<float>(x) + 0.5f) * step - 0.5f;
            auto *pixel = out.data() + static_cast<std::size_t>((y * kCoverSize + x) * 4);
            for (int c = 0; c < 3; ++c)
                pixel[c] = static_cast<std::uint8_t>(std::lround(
                    std::clamp(sample(rgba, width, height, sx, sy, c, wrap), 0.0f, 255.0f)));
            pixel[3] = 255;
        }
    }
    return out;
}
} // namespace

std::vector<std::uint8_t> cover_pixels(const std::vector<std::uint8_t> &rgba, int width, int height)
{
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        return {};
    const int side = std::min(width, height);
    return scaled(rgba, width, height, static_cast<float>(width - side) / 2,
                  static_cast<float>(height - side) / 2, static_cast<float>(side), false);
}

std::vector<std::uint8_t> cover_clouds()
{
    return scaled(cloud_texture(), kCloudTextureSize, kCloudTextureSize, 0, 0,
                  static_cast<float>(kCloudTextureSize), true);
}

void blend_pixels(const std::vector<std::uint8_t> &from, const std::vector<std::uint8_t> &to,
                  float level, std::vector<std::uint8_t> &out)
{
    out.resize(kCoverBytes);
    if (from.size() != kCoverBytes || to.size() != kCoverBytes)
        return;
    const int weight = static_cast<int>(std::lround(std::clamp(level, 0.0f, 1.0f) * 256));
    for (std::size_t i = 0; i < kCoverBytes; ++i)
        out[i] = static_cast<std::uint8_t>(from[i] + (((to[i] - from[i]) * weight) >> 8));
}

void CoverSchedule::want(std::uint64_t picture, double now)
{
    if (picture == target_)
        return;
    target_ = picture;
    changed_at_ = now;
}

void CoverSchedule::touch(double now)
{
    if (target_ != shown_)
        changed_at_ = now;
}

std::optional<CoverSchedule::Fade> CoverSchedule::tick(double now)
{
    if (target_ == shown_ || now - changed_at_ < kSettleMs || now - started_at_ < kGapMs)
        return std::nullopt;
    Fade fade;
    fade.from = shown_;
    fade.to = target_;
    shown_ = target_;
    started_at_ = now;
    return fade;
}

float CoverSchedule::level(double now) const
{
    return static_cast<float>(std::clamp((now - started_at_) / kFadeMs, 0.0, 1.0));
}
} // namespace hui::afterglow
