// Afterglow - Lyrics over visuals: where One line and Stage sit, and their size.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/preferences.hpp"

namespace hui::afterglow
{
// The Display rows' words for an OverlayLook, as LG Afterglow writes them.
inline constexpr const char *kOverlayPositions[] = {"Top", "Middle", "Bottom"};
inline constexpr const char *kOverlayBackgrounds[] = {"Box", "Shade", "None"};
inline constexpr const char *kOverlaySizes[] = {"Small", "Medium", "Large"};
enum OverlayPosition : int
{
    position_top = 0,
    position_middle = 1,
    position_bottom = 2,
};
enum OverlayBackground : int
{
    background_box = 0,
    background_shade = 1,
    background_none = 2,
};

// Text, box and shade scale together: Small 0.8x, Medium today's, Large 1.25x.
float overlay_scale(int size);

// What an overlay must stay clear of, in canvas pixels.
struct OverlayRoom
{
    float top = 0;    // the highest its top edge may go
    float bottom = 0; // the lowest its bottom edge may go (above the hints)
    // The track card's top edge and how visible it is (0..1). The overlay
    // stacks a gutter above it as the card fades in, so it slides rather than
    // jumps, and never overlaps it.
    float card_top = 0;
    float card = 0;
};
// The gap between a stacked overlay and the track card (LG's 1.4vh).
inline constexpr float kOverlayGutter = 16;

// The top edge of an overlay `height` tall: Top under room.top, Middle centred
// on the screen (1080 / 2), Bottom on room.bottom. Middle and Top move up only
// when the card would otherwise run into them; nothing leaves the room.
float overlay_top(int position, float height, const OverlayRoom &room);
} // namespace hui::afterglow
