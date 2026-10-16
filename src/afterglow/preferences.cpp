// Afterglow - Persistent visual controls and preset favourites.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/preferences.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace hui::afterglow
{
bool Preferences::favorite(std::string_view name) const
{
    return std::find(favorites.begin(), favorites.end(), name) != favorites.end();
}
void Preferences::toggle_favorite(const std::string &name)
{
    const auto found = std::find(favorites.begin(), favorites.end(), name);
    if (found != favorites.end())
        favorites.erase(found);
    else if (!name.empty() && name.find_first_of("\r\n") == std::string::npos)
        favorites.push_back(name);
}
namespace
{
// A choice from 0..2, or the default when the saved value is not one.
int choice(int value, int fallback)
{
    return value >= 0 && value <= 2 ? value : fallback;
}
} // namespace
std::string Preferences::encode() const
{
    std::string out;
    const auto number = [&](const char *key, int value)
    { out += std::string(key) + "=" + std::to_string(value) + "\n"; };
    number("follow", follow);
    number("hold_idle", hold_idle);
    number("auto_level", auto_level);
    number("oled_drift", oled_drift);
    number("locked", locked);
    number("favorites_only", favorites_only);
    number("tint", tint);
    number("brightness", brightness);
    number("text_brightness", text_brightness);
    number("fps", fps);
    number("lyrics_layer", lyrics_layer);
    number("poster_visuals", poster_visuals);
    number("visual_style", visual_style);
    number("reduce_motion", reduce_motion);
    number("display_mode", display_mode);
    number("lyrics_offset_ms", lyrics_offset_ms);
    number("trim_ms", trim_ms);
    number("preset_seconds", preset_seconds);
    number("track_info", track_info);
    number("sequential", sequential);
    number("skip_slow", skip_slow);
    number("minimum_fps", minimum_fps);
    number("scan_cursor", scan_cursor);
    number("tint_strength", tint_strength);
    number("beat_sensitivity", beat_sensitivity);
    number("night_register", night_register);
    number("karaoke", karaoke);
    number("lightbar", lightbar);
    number("lightbar_beat", lightbar_beat);
    number("account_sync", account_sync);
    number("online_lyrics", online_lyrics);
    number("line_position", line_look.position);
    number("line_background", line_look.background);
    number("line_size", line_look.size);
    number("stage_position", stage_look.position);
    number("stage_background", stage_look.background);
    number("stage_size", stage_look.size);
    number("cover_art", cover_art);
    number("preset_change", preset_change);
    if (quality.find_first_of("\r\n") == std::string::npos)
        out += "quality=" + quality + "\n";
    if (favorite_pending.find_first_of("\r\n") == std::string::npos)
        out += "favorite_pending=" + favorite_pending + "\n";
    if (player.find_first_of("\r\n") == std::string::npos)
        out += "player=" + player + "\n";
    for (const auto &name : favorites)
        if (name.find_first_of("\r\n") == std::string::npos)
            out += "favorite=" + name + "\n";
    return out;
}
Preferences Preferences::decode(std::string_view text)
{
    Preferences p;
    while (!text.empty())
    {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const auto eq = line.find('=');
        if (eq == line.npos)
            continue;
        const auto key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "quality" && value.size() <= 262144)
            p.quality = value;
        if (key == "favorite_pending" && value.size() <= 262144)
            p.favorite_pending = value;
        if (key == "player" && value.size() <= 256)
            p.player = value;
        if (key == "favorite" && !value.empty() && value.find('\r') == value.npos &&
            value.size() <= 512 && p.favorites.size() < 1000 && !p.favorite(value))
            p.favorites.emplace_back(value);
        int n = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), n);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
            continue;
        if (key == "follow")
            p.follow = n == 1;
        if (key == "hold_idle")
            p.hold_idle = n == 1;
        if (key == "auto_level")
            p.auto_level = n == 1;
        if (key == "oled_drift")
            p.oled_drift = n == 1;
        if (key == "locked")
            p.locked = n == 1;
        if (key == "favorites_only")
            p.favorites_only = n == 1;
        if (key == "tint")
            p.tint = n == 1;
        if (key == "brightness")
            p.brightness = std::clamp(n, 10, 100);
        if (key == "text_brightness")
            p.text_brightness = std::clamp(n, 30, 100);
        if (key == "fps")
            p.fps = n == 30 || n == 120 ? n : 60;
        if (key == "lyrics_layer")
            p.lyrics_layer = std::clamp(n, 0, 2);
        if (key == "poster_visuals")
            p.poster_visuals = n == 1;
        if (key == "visual_style")
            p.visual_style = std::clamp(n, 0, 2);
        if (key == "reduce_motion")
            p.reduce_motion = n == 1;
        if (key == "display_mode")
            p.display_mode = std::clamp(n, 0, 3);
        if (key == "lyrics_offset_ms")
            p.lyrics_offset_ms = std::clamp(n, -10000, 10000);
        if (key == "trim_ms")
            p.trim_ms = std::clamp(n, -2000, 2000);
        if (key == "preset_seconds")
            p.preset_seconds = std::clamp(n, 15, 600);
        if (key == "track_info")
            p.track_info = std::clamp(n, 0, 2);
        if (key == "sequential")
            p.sequential = n == 1;
        if (key == "skip_slow")
            p.skip_slow = n == 1;
        if (key == "minimum_fps")
            p.minimum_fps = n == 30 || n == 40 || n == 45 || n == 60 ? n : 45;
        if (key == "scan_cursor")
            p.scan_cursor = std::clamp(n, 0, 10000);
        if (key == "tint_strength")
            p.tint_strength = n == 25 || n == 50 || n == 75 ? n : 100;
        if (key == "beat_sensitivity")
            p.beat_sensitivity = std::clamp(n, 0, 200);
        if (key == "night_register")
            p.night_register = n == 1;
        if (key == "karaoke")
            p.karaoke = n == 1;
        if (key == "lightbar")
            p.lightbar = n == 1;
        if (key == "lightbar_beat")
            p.lightbar_beat = n == 1;
        if (key == "account_sync")
            p.account_sync = n == 1;
        if (key == "online_lyrics")
            p.online_lyrics = n == 1;
        if (key == "line_position")
            p.line_look.position = choice(n, kLineLookDefault.position);
        if (key == "line_background")
            p.line_look.background = choice(n, kLineLookDefault.background);
        if (key == "line_size")
            p.line_look.size = choice(n, kLineLookDefault.size);
        if (key == "stage_position")
            p.stage_look.position = choice(n, kStageLookDefault.position);
        if (key == "stage_background")
            p.stage_look.background = choice(n, kStageLookDefault.background);
        if (key == "stage_size")
            p.stage_look.size = choice(n, kStageLookDefault.size);
        if (key == "cover_art")
            p.cover_art = n == 1;
        if (key == "preset_change")
            p.preset_change = choice(n, 0);
    }
    return p;
}
namespace
{
constexpr std::array<std::string_view, 37> kSyncedKeys = {
    "follow",           "hold_idle",      "auto_level",     "oled_drift",       "locked",
    "favorites_only",   "tint",           "brightness",     "text_brightness",  "fps",
    "lyrics_layer",     "poster_visuals", "visual_style",   "reduce_motion",    "display_mode",
    "lyrics_offset_ms", "trim_ms",        "preset_seconds", "track_info",       "sequential",
    "skip_slow",        "minimum_fps",    "tint_strength",  "beat_sensitivity", "night_register",
    "karaoke",          "lightbar",       "lightbar_beat",  "online_lyrics",    "line_position",
    "line_background",  "line_size",      "stage_position", "stage_background", "stage_size", "cover_art", "preset_change"};
bool synced_key(std::string_view key)
{
    return std::find(kSyncedKeys.begin(), kSyncedKeys.end(), key) != kSyncedKeys.end();
}
void skip_space(std::string_view s, std::size_t &at)
{
    while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at])))
        ++at;
}
// One "key": value pair of the flat object written by encode_synced. Keys are
// plain snake_case; values are integers or booleans. Anything else stops the
// scan, keeping what was read before it.
bool next_pair(std::string_view s, std::size_t &at, std::string_view &key, std::string &value)
{
    skip_space(s, at);
    if (at >= s.size() || s[at] != '"')
        return false;
    const auto start = ++at;
    while (at < s.size() && at - start <= 40 &&
           (std::islower(static_cast<unsigned char>(s[at])) || s[at] == '_'))
        ++at;
    if (at >= s.size() || s[at] != '"')
        return false;
    key = s.substr(start, at - start);
    ++at;
    skip_space(s, at);
    if (at >= s.size() || s[at++] != ':')
        return false;
    skip_space(s, at);
    if (s.substr(at, 4) == "true" || s.substr(at, 5) == "false")
    {
        value = s[at] == 't' ? "1" : "0";
        at += s[at] == 't' ? 4 : 5;
    }
    else
    {
        const auto number = at;
        if (at < s.size() && s[at] == '-')
            ++at;
        while (at < s.size() && at - number < 11 && std::isdigit(static_cast<unsigned char>(s[at])))
            ++at;
        value = std::string(s.substr(number, at - number));
        int parsed = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
            return false;
    }
    skip_space(s, at);
    if (at < s.size() && s[at] == ',')
        ++at;
    else if (at >= s.size() || s[at] != '}')
        return false;
    return true;
}
} // namespace
std::string Preferences::encode_synced() const
{
    std::string out = "{";
    const auto text = encode();
    std::string_view rest = text;
    while (!rest.empty())
    {
        const auto end = rest.find('\n');
        const auto line = rest.substr(0, end);
        rest = end == rest.npos ? std::string_view{} : rest.substr(end + 1);
        const auto eq = line.find('=');
        if (eq == line.npos || !synced_key(line.substr(0, eq)))
            continue;
        if (out.size() > 1)
            out += ',';
        out += '"';
        out += line.substr(0, eq);
        out += "\":";
        out += line.substr(eq + 1);
    }
    return out + '}';
}
Preferences Preferences::with_synced(std::string_view json) const
{
    // Applied through decode() so the clamps live in one place.
    std::string text = encode();
    std::size_t at = 0;
    skip_space(json, at);
    if (json.size() > 16384 || at >= json.size() || json[at++] != '{')
        return *this;
    std::string_view key;
    std::string value;
    for (std::size_t pairs = 0; pairs < 64 && next_pair(json, at, key, value); ++pairs)
        if (synced_key(key))
            text += std::string(key) + "=" + value + "\n";
    return decode(text);
}
} // namespace hui::afterglow
