// Afterglow - The poster, the track card, the lyric layers and the controls band.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "afterglow/idle.hpp"
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
    // The playback position in seconds as the progress marks draw it: moved
    // every frame by a ProgressClock, not stepped by status updates.
    double position = 0;
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

// The width untimed lyrics wrap to in the Lyrics poster's column; the screen
// pages them by it.
inline constexpr float kPlainLyricsWidth = 998;

// The halo and spectrum styles; MilkDrop arrives as a texture.
void draw_visual_style(const Scene &scene, int style);
// Album (lyrics = false) or Lyrics poster, as LG sets it: the cover in a
// square at the left margin, the words in one column on the dark ground. ground
// is the ground's opacity: 1 over nothing, less over MilkDrop. With no song it
// is the setup, connecting or waiting screen.
void draw_poster(const Scene &scene, bool lyrics, float ground);
// The poster's left side (ground, place line, cover) and the ink a sheet
// stands on from kSlabWidth, for sheets that sit right of the artwork.
void draw_slab(const Scene &scene, float ground);
void draw_track_card(const Scene &scene);
// The track card's height (it stands on y = 980), for overlays that stack above it.
float track_card_height(const ui::Fonts &fonts, const Presentation &state);
// The lyric overlays, laid out by their OverlayLook in the preferences
// (position, background, size) and kept clear of the track card.
void draw_lyric_line(const Scene &scene);
void draw_stage(const Scene &scene);
// Centred below the poster header and the section strip, or in the top-right
// corner of a sheet, where the title leaves room.
void draw_toast(const Scene &scene, bool corner);

// Width the one-line lyric field wants for the current line at a size's scale.
float lyric_line_width(const ui::Fonts &fonts, const LyricView &lyrics, float scale = 1);

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

// The idle poster (idle.hpp), handed over by the screen's update().
struct IdleView
{
    const std::vector<IdleTile> *tiles = nullptr;
    int focus = 0;
    IdleText text;
    // Where Cross acts (the command target) and what state the music is in.
    std::string where, status, note;
    float level = 1; // IdlePoster::level(): 1 awake, dimmer at rest
};
// Full screen, LG's #idle: a cover box, the title, one field and the strip.
// Only the paused song wears its own colours; every other tile is set on the
// neutral fields.
void draw_idle_poster(const Scene &scene, const IdleView &idle);
// Paper and secondary text as the poster sets them (stepped down in the night
// register), for pages drawn outside this file (programme_scene.cpp).
gfx::Color poster_paper(const Presentation &state);
gfx::Color poster_muted(const Presentation &state);
} // namespace hui::afterglow
