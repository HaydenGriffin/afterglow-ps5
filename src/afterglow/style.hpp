// Afterglow - Poster tokens shared by every screen: colours, grid, type, fields.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include <algorithm>
#include <cmath>

// The LG Tonhalle poster adapted to a 1920 x 1080 canvas seen from a sofa:
// warm paper on ink, square fields coloured from the artwork, Archivo.
// Nothing that stays on screen is smaller than kTypeMin; only toasts and
// transient hints may use kTypeTransient.
namespace hui::afterglow::style
{
inline const gfx::Color kInk = gfx::Color::rgb(0x121417);
inline const gfx::Color kPaper = gfx::Color::rgb(0xefebe3);
inline const gfx::Color kMuted = kPaper.with_alpha(0.68f);
inline const gfx::Color kFaint = kPaper.with_alpha(0.5f);
inline const gfx::Color kRule = kPaper.with_alpha(0.22f);
// A focused row that refuses Cross: rule fill with paper text, never muted on paper.
inline const gfx::Color kDisabledFill = gfx::Color::rgb(0x34363a);
// Before artwork arrives the fields are neutral, never a white slab on an OLED.
inline const gfx::Color kNeutralA = gfx::Color::rgb(0x3e4248);
inline const gfx::Color kNeutralB = gfx::Color::rgb(0x26292e);

// Title-safe area: 5 % of the canvas.
constexpr float kSafeLeft = 96;
constexpr float kSafeRight = 1824;
constexpr float kSafeTop = 54;
constexpr float kSafeBottom = 1026;
// The poster's artwork slab and the column everything beside it aligns to.
constexpr float kSlabWidth = 656;
constexpr float kColumn = 680;
constexpr float kFieldInset = 28;

constexpr float kTypeTransient = 24;
constexpr float kTypeMin = 26;
constexpr float kTypeBody = 28;
constexpr float kTypeLabel = 30;
constexpr float kTypeSheetTitle = 65;
constexpr float kTypeBrowseTitle = 97;

// Fades and lyric lines start fast and settle softly.
constexpr float kFadeSeconds = 0.5f;
inline float ease_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - std::pow(1.0f - t, 3.0f);
}
// Exponential approach for springs that track a target each frame.
inline float approach(float value, float target, float dt, float rate = 12.0f)
{
    return value + (target - value) * (1.0f - std::exp(-dt * rate));
}

inline float luminance(gfx::Color c)
{
    const auto linear = [](float value)
    { return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * linear(c.r) + 0.7152f * linear(c.g) + 0.0722f * linear(c.b);
}
inline float contrast(gfx::Color a, gfx::Color b)
{
    const float x = luminance(a) + 0.05f, y = luminance(b) + 0.05f;
    return x > y ? x / y : y / x;
}
// Darkens an artwork colour in 15 % steps until ink or paper reads on it at 4.5:1.
inline gfx::Color readable_field(gfx::Color c)
{
    c.a = 1;
    for (int i = 0; i < 12; ++i)
    {
        if (std::max(contrast(c, kInk), contrast(c, kPaper)) >= 4.5f)
            break;
        c.r *= 0.85f;
        c.g *= 0.85f;
        c.b *= 0.85f;
    }
    return c;
}
inline gfx::Color field_text(gfx::Color field)
{
    return contrast(field, kInk) >= contrast(field, kPaper) ? kInk : kPaper;
}
} // namespace hui::afterglow::style
