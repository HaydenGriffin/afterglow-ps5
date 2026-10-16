// Afterglow - DualSense light bar colour from the artwork and the beat.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/lightbar.hpp"

#include <algorithm>
#include <cmath>

namespace hui::afterglow
{
namespace
{
// The amber the light bar shows when the artwork has no colour to give.
constexpr std::array<float, 3> kAmber{238.0f / 255, 184.0f / 255, 117.0f / 255};
constexpr float kPlaying = 1.0f, kIdle = 0.12f;
// On the beat the bar rests at kBeatFloor and flashes to full brightness.
constexpr float kBeatFloor = 0.35f;

std::uint8_t channel(float value)
{
    if (!std::isfinite(value))
        return 0;
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}
} // namespace

LightbarColor lightbar_color(const LightbarInput &input)
{
    std::array<float, 3> hue = input.artwork;
    const float peak = std::max({hue[0], hue[1], hue[2]});
    if (!(peak >= 0.02f))
        hue = kAmber;
    else
        for (float &value : hue)
            value = std::clamp(value / peak, 0.0f, 1.0f);
    float level = kIdle;
    if (input.playing)
        level = input.beat_enabled
                    ? kBeatFloor + (kPlaying - kBeatFloor) * std::clamp(input.beat, 0.0f, 1.0f)
                    : kPlaying;
    return {channel(hue[0] * level), channel(hue[1] * level), channel(hue[2] * level)};
}

bool LightbarLimiter::due(const LightbarColor &next, std::int64_t now_us) const
{
    if (!written_)
        return true;
    return next != last_ && now_us - last_us_ >= kMinIntervalUs;
}

void LightbarLimiter::wrote(const LightbarColor &color, std::int64_t now_us)
{
    last_ = color;
    last_us_ = now_us;
    written_ = true;
}

void LightbarLimiter::forget()
{
    written_ = false;
}
} // namespace hui::afterglow
