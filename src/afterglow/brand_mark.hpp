// Afterglow - The wordmark on the screens you pass through: setup, connecting and Status.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include <cstdint>
#include <functional>
#include <string>

// Usage (UI thread, where GL lives):
//   model.wordmark = wordmark_hook("/app0/assets/brand/afterglow-wordmark-paper.png", upload);
//   scenes:  if (state.wordmark) state.wordmark(list, x, baseline, 400);
// The picture is read, decoded and uploaded on the first draw, once; a missing
// or broken file draws nothing and is not tried again. Never on the posters:
// a logo that sits still for hours marks an OLED.
namespace hui::afterglow
{
// The picture's shape (assets/brand/afterglow-wordmark-paper.png, 960 x 193)
// and its baseline as a share of its height: the sun's lower part sits below
// the baseline, cut off by the horizon (docs/brand/README.md).
inline constexpr float kWordmarkAspect = 193.0f / 960.0f;
inline constexpr float kWordmarkBaseline = 138.0f / 175.0f;
// Uploads RGBA8 pixels as a texture and returns its name (GlBatch::create_texture).
using TextureUpload = std::function<std::uint32_t(int width, int height, const std::uint8_t *rgba)>;
// Draws the wordmark width pixels wide with its baseline (the horizon the sun
// sets behind) at y; x is the left edge.
using WordmarkDraw = std::function<void(gfx::DrawList &list, float x, float baseline, float width)>;

class BrandMark
{
  public:
    BrandMark(std::string path, TextureUpload upload);
    void draw(gfx::DrawList &list, float x, float baseline, float width);
    // Height for a width, from the picture's aspect (0 until it has loaded).
    float height(float width) const;

  private:
    void load();
    std::string path_;
    TextureUpload upload_;
    bool tried_ = false;
    // The picture and a half-size copy: GL samples without mipmaps here, so a
    // small mark reads from the smaller copy instead of skipping texels.
    std::uint32_t full_ = 0, half_ = 0;
    int width_ = 0, height_ = 0;
};

// The Presentation::wordmark hook, owning a BrandMark for as long as it lives.
WordmarkDraw wordmark_hook(std::string path, TextureUpload upload);
} // namespace hui::afterglow
