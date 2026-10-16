// Afterglow - Album colour for MilkDrop: the preset's own colour slots rewritten.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/preset_colour.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace hui::afterglow
{
namespace
{
constexpr const char *kSlotKeys[4][3] = {{"wave_r", "wave_g", "wave_b"},
                                         {"ob_r", "ob_g", "ob_b"},
                                         {"ib_r", "ib_g", "ib_b"},
                                         {"mv_r", "mv_g", "mv_b"}};
// MilkDrop's defaults for a slot the preset leaves out.
constexpr float kSlotDefaults[4][3] = {{1, 1, 1}, {0, 0, 0}, {0.25f, 0.25f, 0.25f}, {1, 1, 1}};
constexpr const char *kSlotSize[4] = {nullptr, "ob_size", "ib_size", nullptr};
constexpr float kSizeDefault = 0.01f;
constexpr float kMaxBorder = 0.05f;
constexpr float kMinChroma = 40;
constexpr float kLuma[3] = {0.3f, 0.59f, 0.11f};

float luma(const Rgb &c)
{
    return c[0] * kLuma[0] + c[1] * kLuma[1] + c[2] * kLuma[2];
}

// color (0-255) with its brightness moved to target, pulled toward the grey of
// that brightness so no channel leaves 0-255: LG's atLuma.
Rgb at_luma(Rgb color, float target)
{
    const float shift = target - luma(color);
    for (float &v : color)
        v += shift;
    const float high = std::max({color[0], color[1], color[2]});
    const float low = std::min({color[0], color[1], color[2]});
    if (high > 255 && high > target)
        for (float &v : color)
            v = target + (v - target) * ((255 - target) / (high - target));
    if (low < 0 && low < target)
        for (float &v : color)
            v = target + (v - target) * (target / (target - low));
    return color;
}

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

// One "key=value" line of the preset, by byte range (line ending excluded).
struct Assignment
{
    std::string key;
    std::size_t begin = 0, end = 0;
    float value = 0;
};

std::vector<Assignment> assignments(std::string_view preset)
{
    std::vector<Assignment> out;
    for (std::size_t start = 0; start < preset.size();)
    {
        auto stop = preset.find('\n', start);
        if (stop == preset.npos)
            stop = preset.size();
        std::size_t end = stop;
        if (end > start && preset[end - 1] == '\r')
            --end;
        const auto line = preset.substr(start, end - start);
        const auto eq = line.find('=');
        if (eq != line.npos)
        {
            Assignment item;
            item.key = lower(trim(line.substr(0, eq)));
            item.begin = start;
            item.end = end;
            const std::string value(trim(line.substr(eq + 1)));
            char *parsed = nullptr;
            item.value = std::strtof(value.c_str(), &parsed);
            if (parsed != value.c_str())
                out.push_back(std::move(item));
        }
        start = stop + 1;
    }
    return out;
}

float value_of(const std::vector<Assignment> &lines, std::string_view key, float fallback)
{
    float value = fallback;
    for (const auto &line : lines)
        if (line.key == key)
            value = line.value;
    return value;
}
} // namespace

bool has_hue(const Rgb &colour)
{
    const float high = std::max({colour[0], colour[1], colour[2]});
    const float low = std::min({colour[0], colour[1], colour[2]});
    return (high - low) * 255 >= kMinChroma;
}

std::string recolour_preset(std::string_view preset, const Rgb &accent, const Rgb &secondary,
                            float strength)
{
    strength = std::clamp(strength, 0.0f, 1.0f);
    std::vector<Rgb> hues;
    for (const Rgb *colour : {&accent, &secondary})
        if (has_hue(*colour))
            hues.push_back({(*colour)[0] * 255, (*colour)[1] * 255, (*colour)[2] * 255});
    if (hues.empty() || strength <= 0)
        return std::string(preset);
    const auto lines = assignments(preset);
    // New text for each slot key; empty slots keep the preset's line.
    std::vector<std::pair<std::string, std::string>> rewritten;
    for (int slot = 0; slot < 4; ++slot)
    {
        if (kSlotSize[slot] != nullptr &&
            value_of(lines, kSlotSize[slot], kSizeDefault) > kMaxBorder)
            continue;
        Rgb own{};
        for (int i = 0; i < 3; ++i)
            own[static_cast<std::size_t>(i)] =
                std::clamp(value_of(lines, kSlotKeys[slot][i], kSlotDefaults[slot][i]), 0.0f, 1.0f);
        const Rgb scaled{own[0] * 255, own[1] * 255, own[2] * 255};
        const Rgb target =
            at_luma(hues[static_cast<std::size_t>(slot % 2) % hues.size()], luma(scaled));
        for (int i = 0; i < 3; ++i)
        {
            const auto c = static_cast<std::size_t>(i);
            const float value = own[c] + (target[c] / 255 - own[c]) * strength;
            char text[48];
            std::snprintf(text, sizeof(text), "%s=%.6f", kSlotKeys[slot][i],
                          static_cast<double>(std::clamp(value, 0.0f, 1.0f)));
            rewritten.emplace_back(kSlotKeys[slot][i], text);
        }
    }
    std::string out;
    out.reserve(preset.size() + 512);
    std::vector<bool> written(rewritten.size(), false);
    std::size_t copied = 0;
    for (const auto &line : lines)
        for (std::size_t i = 0; i < rewritten.size(); ++i)
            if (line.key == rewritten[i].first)
            {
                out.append(preset.substr(copied, line.begin - copied));
                out += rewritten[i].second;
                copied = line.end;
                written[i] = true;
            }
    out.append(preset.substr(copied));
    // Keys the preset left at MilkDrop's defaults are added at the end, in the
    // file's own line endings.
    const std::string_view newline = preset.find("\r\n") != preset.npos ? "\r\n" : "\n";
    for (std::size_t i = 0; i < rewritten.size(); ++i)
        if (!written[i])
        {
            if (!out.empty() && out.back() != '\n')
                out += newline;
            out += rewritten[i].second;
            out += newline;
        }
    return out;
}
} // namespace hui::afterglow
