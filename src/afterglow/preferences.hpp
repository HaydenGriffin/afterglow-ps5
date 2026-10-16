// Afterglow - Persistent visual controls and preset favourites.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace hui::afterglow
{
// Lyrics over visuals: how one overlay (One line or Stage) is laid out. Each
// value is an index into the Display rows' choices; one out of range reads as
// the overlay's default.
struct OverlayLook
{
    int position = 2;   // Top, Middle, Bottom
    int background = 0; // Box, Shade, None
    int size = 1;       // Small, Medium, Large
    bool operator==(const OverlayLook &) const = default;
};
// One line sits at the bottom, Stage in the middle; both on today's box at today's size.
inline constexpr OverlayLook kLineLookDefault{2, 0, 1};
inline constexpr OverlayLook kStageLookDefault{1, 0, 1};

struct Preferences
{
    bool follow = false, hold_idle = true, auto_level = false, oled_drift = true;
    bool locked = false, favorites_only = false, tint = false;
    bool sequential = false, skip_slow = true;
    // Lyrics over the visuals: off, one line in a field, or the centred stage.
    int lyrics_layer = 0;
    // Words-only lyric timing, -10000..10000 ms in 100 ms steps; positive is later.
    int lyrics_offset_ms = 0;
    // MilkDrop keeps playing behind the Album and Lyrics posters.
    bool poster_visuals = true;
    // MilkDrop, halo or spectrum.
    int visual_style = 0;
    bool reduce_motion = false;
    // Visuals only, visuals with track, album poster, synchronized lyrics.
    int display_mode = 1;
    // 120 fps requests a 120 Hz display mode on next launch; 60 remains the default.
    int brightness = 100, text_brightness = 100, fps = 60, trim_ms = 0;
    // Track card: always, for nine seconds at each track change, or hidden.
    int preset_seconds = 120, track_info = 1;
    int minimum_fps = 45, scan_cursor = 0;
    // Album colour strength 25/50/75/100 %; tint recolours the preset's parts (LG).
    int tint_strength = 100;
    // projectM beat sensitivity in percent (projectm_set_beat_sensitivity / 100), 0..200.
    int beat_sensitivity = 100;
    // Dimmer poster register 22:00-06:00 local time, like the LG app.
    bool night_register = true;
    // Word-by-word highlight when the LRC carries enhanced word timings.
    bool karaoke = true;
    // DualSense light bar follows the artwork colour; pulses on the beat when lightbar_beat.
    bool lightbar = true, lightbar_beat = false;
    // Account mirroring of these preferences to Music Assistant (vizman.devices[id]).
    bool account_sync = true;
    // Online lyrics (LG onlineLyrics): when Music Assistant has none, ask
    // lrclib.net. Off sends nothing there.
    bool online_lyrics = true;
    // Position, background and size of each lyric overlay, kept apart.
    OverlayLook line_look = kLineLookDefault, stage_look = kStageLookDefault;
    // The song's cover in the presets that sample a picture (cover_texture.hpp).
    bool cover_art = true;
    // Change preset: 0 on the preset_seconds timer, 1 each song, 2 on big moments (preset_change.hpp).
    int preset_change = 0;
    std::string player, quality, favorite_pending;
    std::vector<std::string> favorites;
    bool favorite(std::string_view name) const;
    void toggle_favorite(const std::string &name);
    std::string encode() const;
    static Preferences decode(std::string_view text);
    // Account mirroring: the settings worth restoring on another install, as a
    // flat JSON object of integers (booleans are 0/1). Not the scan cursor,
    // preset ratings, favourite journal, followed player or favourites, which
    // are local or synced separately; nor account_sync itself.
    std::string encode_synced() const;
    // This with the synced keys of json applied (bounded; unknown keys and
    // malformed values are ignored, values clamped as decode() does).
    Preferences with_synced(std::string_view json) const;
};
} // namespace hui::afterglow
