// Afterglow - Stroked line icons for the transport band and sheets.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"

namespace hui::afterglow
{
// Drawn with DrawList lines and arcs in the LG app's outline style: round
// joins, one stroke weight, no fills except where noted. Every icon fits a
// size x size square centred on (cx, cy).
enum class Icon
{
    previous,     // bar then a left-pointing triangle outline
    play,         // right-pointing triangle outline
    pause,        // two vertical bars
    next,         // right-pointing triangle outline then a bar
    back10,       // two left chevrons
    ahead10,      // two right chevrons
    heart,        // heart outline
    heart_filled, // heart, filled
    details,      // four left-aligned lines of falling length
    display,      // monitor outline on a stand
    lyrics,       // speech mark with two lines
    check,        // tick
    count,
};

void draw_icon(gfx::DrawList &list, Icon icon, float cx, float cy, float size, gfx::Color colour,
               float stroke = 3.0f);
} // namespace hui::afterglow
