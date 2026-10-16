// Afterglow - The song's cover as the picture MilkDrop presets sample, and when it changes.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace hui::afterglow
{
// 44 presets sample pictures that are not shipped (cloud_texture.hpp). With
// "Cover art in the visuals" on they get the song's cover instead, in one
// fixed texture of this size that is updated in place (Milkdrop owns it).
// LG Afterglow does the same with the same numbers (src/cover-art.js).
constexpr int kCoverSize = 512;

// RGBA8 kCoverSize x kCoverSize: the middle square of an RGBA picture (first
// row at the top), scaled bilinearly, first row at the BOTTOM as OpenGL and
// projectM expect. Empty for an empty or malformed picture.
std::vector<std::uint8_t> cover_pixels(const std::vector<std::uint8_t> &rgba, int width,
                                       int height);
// The generated clouds at kCoverSize, scaled up so they still tile.
std::vector<std::uint8_t> cover_clouds();
// out = from + (to - from) * level, per channel; all three kCoverSize RGBA.
void blend_pixels(const std::vector<std::uint8_t> &from, const std::vector<std::uint8_t> &to,
                  float level, std::vector<std::uint8_t> &out);

// Which picture to show and when. Pictures are numbered; 0 is the clouds.
// A change waits until the picture has arrived and the colours have not
// moved for 1 s, then fades over 2 s; at most one fade starts every 4 s, and
// the newest picture waiting is the one shown. Times in milliseconds.
class CoverSchedule
{
  public:
    struct Fade
    {
        std::uint64_t from = 0, to = 0;
    };
    void want(std::uint64_t picture, double now);
    // The colours changed: a picture still waiting waits for them to settle.
    void touch(double now);
    std::optional<Fade> tick(double now);
    // How far the current fade has come, 0 to 1.
    float level(double now) const;
    std::uint64_t shown() const
    {
        return shown_;
    }

  private:
    std::uint64_t target_ = 0, shown_ = 0;
    double changed_at_ = -1e18, started_at_ = -1e18;
};
} // namespace hui::afterglow
