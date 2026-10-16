// Afterglow - Tonight's programme drawn as a concert programme on the idle poster.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/programme.hpp"
#include "afterglow/scenes.hpp"

namespace hui::afterglow
{
// Full screen, ink, on the poster grid (LG programme-view.js): the queue's
// name and the count on a faded top line over a hairline; the headline word
// and the evening's date in columns 1-4; the songs in two ruled columns over
// columns 5-12; Keep it as a playlist and Back at the foot of columns 1-4.
// level is the idle poster's (IdlePoster::level()): the page keeps its dim
// register, and the screen's OLED drift moves it with everything else.
void draw_programme(const Scene &scene, const ProgrammePage &page, float level);
} // namespace hui::afterglow
