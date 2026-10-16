// Afterglow - The poster, the track card, the lyric layers and the controls band.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "afterglow/lyrics.hpp"
#include "afterglow/presentation.hpp"
#include "ui/fonts.hpp"
#include <vector>

// Pure drawing: the Afterglow screen advances every timer in update() and
// hands them over here, so draw() stays const and these functions need no GL.
namespace hui::afterglow
{
struct SceneClock
{
    float time = 0;        // seconds; frozen while motion is reduced
    float track_age = 99;  // since the title last changed
    float lyric_age = 99;  // since the active timed line last changed
    float card = 0;        // track card visibility, 0..1
    float lyric = 0;       // one-line lyric field visibility, 0..1
    float lyric_width = 0; // that field's sprung width
    bool reduced_motion = false;
};

struct LyricView
{
    const std::vector<LyricLine> *lines = nullptr;
    bool timed = false;
    int active = -1; // current timed line, -1 before the first
    double now = 0;  // track position minus the lyric offset
    int page = 0;    // plain lyrics: the page shown
    int pages = 1;   // plain lyrics: how many pages
    int first = 0;   // plain lyrics: first line on the page
    int count = 0;   // plain lyrics: lines on the page
    bool loading = false;
    bool karaoke = false;  // light words as they are sung, when the line has timings
    bool failed = false;   // the lookup failed (retryable)
    bool retrying = false; // and will try once more by itself
    bool empty() const
    {
        return lines == nullptr || lines->empty();
    }
};

struct Scene
{
    gfx::DrawList &list;
    const ui::Fonts &fonts;
    const Presentation &state;
    const SceneClock &clock;
    const LyricView &lyrics;
};

// Field colours: A from the artwork's accent, B from its second colour, both
// darkened until ink or paper reads on them.
gfx::Color field_a(const Presentation &state);
gfx::Color field_b(const Presentation &state);
// The night register is on: preferences allow it and it is 22:00-06:00.
// Fields, paper and artwork step down (LG player.css #player.night).
bool night_register(const Presentation &state);

// What fills the screen when there is no song: setup, connecting or idle.
struct IdleCopy
{
    std::string title, line, note;
};
IdleCopy idle_copy(const Presentation &state);

// The halo and spectrum styles; MilkDrop arrives as a texture.
void draw_visual_style(const Scene &scene, int style);
// Album (lyrics = false) or Lyrics poster beside the artwork slab. ground is
// the opacity of the ink behind the text: 1 over nothing, 0.82 over MilkDrop.
void draw_poster(const Scene &scene, bool lyrics, float ground);
// Only the slab and ground, for sheets that sit right of the artwork.
void draw_slab(const Scene &scene, float ground);
void draw_track_card(const Scene &scene);
void draw_lyric_line(const Scene &scene);
void draw_stage(const Scene &scene);
// Centred below the poster header and the section strip, or in the top-right
// corner of a sheet, where the title leaves room.
void draw_toast(const Scene &scene, bool corner);

// Width the one-line lyric field wants for the current line.
float lyric_line_width(const ui::Fonts &fonts, const LyricView &lyrics);

struct Transport
{
    float open = 0;  // 0..1 slide
    int row = 1;     // 0 the section strip, 1 the tiles
    int section = 1; // Browse, Now playing, Queue, Settings, Home
    int tile = 1;    // focused tile
    int focus_section = 1;
};
enum class Tile
{
    previous,
    play_pause,
    next,
    back10,
    ahead10,
    favourite,
    details,
    display,
    count,
};
void draw_transport(const Scene &scene, const Transport &transport);
const char *section_name(int section);
// Four sections, five once Home Assistant is set up (Home comes last).
int section_count(const Presentation &state);
} // namespace hui::afterglow
