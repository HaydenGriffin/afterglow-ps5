// Afterglow - Persistent visual controls and preset favourites.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace hui::afterglow
{
struct Preferences
{
    bool follow = false, hold_idle = true, auto_level = false, oled_drift = true;
    bool locked = false, favorites_only = false, tint = false;
    bool sequential = false, skip_slow = true;
    // Lyrics over the visuals: off, one line in a field, or the centred stage.
    int lyrics_layer = 0;
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
