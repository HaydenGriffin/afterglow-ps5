// Afterglow - DualSense light bar colour from the artwork and the beat.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>

namespace hui::afterglow
{
struct LightbarColor
{
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const LightbarColor &) const = default;
};

struct LightbarInput
{
    // The artwork's field colour, 0..1 per channel (Presentation::artwork_color).
    std::array<float, 3> artwork{};
    bool playing = false;
    // Pulse with beat (0..1, decaying) instead of glowing steadily.
    bool beat_enabled = false;
    float beat = 0;
};

// The artwork colour lifted to full brightness with its hue kept, so a dark
// cover still glows; near-black falls back to the Afterglow amber. Playing
// glows steadily (or pulses on the beat); idle is a dim glow.
LightbarColor lightbar_color(const LightbarInput &input);

// Rate limit for scePadSetLightBar: a write is due only when the colour
// changed and at least min_interval_us has passed since the last write.
class LightbarLimiter
{
  public:
    static constexpr std::int64_t kMinIntervalUs = 1000000 / 30;

    bool due(const LightbarColor &next, std::int64_t now_us) const;
    void wrote(const LightbarColor &color, std::int64_t now_us);
    // Forget the last write (after a reset), so the next colour is sent.
    void forget();
    // Every frame, whether the controller is there. A controller that comes
    // back has the system's colour again: true then, and the last write is
    // forgotten, so the caller sends the colour once more.
    bool reconnected(bool connected);

  private:
    LightbarColor last_;
    std::int64_t last_us_ = 0;
    bool written_ = false;
    bool connected_ = true;
};
} // namespace hui::afterglow
