// Afterglow - Album colour for MilkDrop: the preset's own colour slots rewritten.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <array>
#include <string>
#include <string_view>

// LG viz-man recolours four Butterchurn slots (src/milkdrop.js): waveform,
// outer border, inner border and motion vectors, each taking an artwork hue
// at the brightness the preset gave that slot. A preset draws those slots
// into its feedback texture, so a light colour where it meant a dark one
// floods the picture; keeping the brightness keeps the preset. A border wider
// than 0.05 is a wash the preset builds on, and keeps its own colour.
//
// projectM has no palette hook, so the PS5 rewrites the preset's base values
// (wave_r/g/b, ob_*, ib_*, mv_*) in the .milk text before loading it. Like
// LG, per-frame equations that assign those variables still win.
//
// LG fills the slots from Music Assistant's palette roles; the PS5 artwork
// decoder gives two colours. The accent (artwork_color) takes the waveform
// and inner border, the secondary (artwork_secondary) the outer border and
// motion vectors. A colour flatter than LG's chroma floor (40 of 255) carries
// no hue and is skipped; with neither left the preset is unchanged.
namespace hui::afterglow
{
using Rgb = std::array<float, 3>;
// strength 0..1 blends from the preset's own colours to the recoloured ones.
std::string recolour_preset(std::string_view preset, const Rgb &accent, const Rgb &secondary,
                            float strength);
// Whether a colour has enough hue to recolour with (LG's MIN_CHROMA).
bool has_hue(const Rgb &colour);
} // namespace hui::afterglow
