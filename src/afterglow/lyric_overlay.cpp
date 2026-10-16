// Afterglow - Lyrics over visuals: where One line and Stage sit, and their size.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/lyric_overlay.hpp"
#include <algorithm>

namespace hui::afterglow
{
float overlay_scale(int size)
{
    return size == 0 ? 0.8f : size == 2 ? 1.25f : 1.0f;
}

float overlay_top(int position, float height, const OverlayRoom &room)
{
    float bottom = room.bottom;
    const float card = std::clamp(room.card, 0.0f, 1.0f);
    if (card > 0)
        bottom =
            std::min(bottom, room.bottom + (room.card_top - kOverlayGutter - room.bottom) * card);
    const float wanted = position == position_top      ? room.top
                         : position == position_middle ? 540 - height / 2
                                                       : bottom - height;
    // Taller than the room: the top edge wins, so the text starts on screen.
    return std::max(room.top, std::min(wanted, bottom - height));
}
} // namespace hui::afterglow
