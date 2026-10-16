// Afterglow - Development requests: quit, screenshot and status from a PC.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/dev_requests.hpp"

#include "afterglow/protocol.hpp"

#include <cmath>
#include <cstdio>

namespace hui::afterglow
{
namespace
{
bool word_char(char c, bool dot)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_' || (dot && c == '.');
}

bool all_word_chars(std::string_view text, bool dot)
{
    for (char c : text)
        if (!word_char(c, dot))
            return false;
    return true;
}

// Splits on single spaces; any other whitespace inside the line is invalid.
std::vector<std::string_view> words(std::string_view line)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= line.size() && out.size() <= 4)
    {
        const auto end = line.find(' ', start);
        out.push_back(line.substr(start, end == std::string_view::npos ? end : end - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return out;
}

bool same_token(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}

void put32(std::vector<std::uint8_t> &out, std::size_t at, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
}
} // namespace

bool valid_dev_token(std::string_view token)
{
    return token.size() >= 16 && token.size() <= 128 && all_word_chars(token, true);
}

bool valid_dev_name(std::string_view name)
{
    return !name.empty() && name.size() <= 48 && all_word_chars(name, false);
}

std::optional<DevRequest> parse_dev_request(std::string_view text, std::string_view token)
{
    if (!valid_dev_token(token))
        return std::nullopt;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.remove_suffix(1);
    if (text.find('\n') != std::string_view::npos)
        return std::nullopt;
    const auto parts = words(text);
    if (parts.size() != 4 || !same_token(parts[2], token) || !valid_dev_name(parts[3]))
        return std::nullopt;
    DevRequest request;
    request.id = std::string(parts[3]);
    if (parts[0] == "screenshot")
    {
        if (!valid_dev_name(parts[1]))
            return std::nullopt;
        request.verb = DevRequest::Verb::screenshot;
        request.argument = std::string(parts[1]);
        return request;
    }
    if (parts[0] == "press")
    {
        if (dev_press_actions(parts[1]).empty())
            return std::nullopt;
        request.verb = DevRequest::Verb::press;
        request.argument = std::string(parts[1]);
        return request;
    }
    if (parts[1] != "-")
        return std::nullopt;
    if (parts[0] == "quit")
        request.verb = DevRequest::Verb::quit;
    else if (parts[0] == "status")
        request.verb = DevRequest::Verb::status;
    else
        return std::nullopt;
    return request;
}

std::optional<Action> dev_press_action(std::string_view button)
{
    struct Name
    {
        std::string_view name;
        Action action;
    };
    static constexpr Name kButtons[] = {
        {"up", Action::up},          {"down", Action::down},      {"left", Action::left},
        {"right", Action::right},    {"cross", Action::confirm},  {"circle", Action::back},
        {"triangle", Action::north}, {"square", Action::west},    {"l1", Action::page_prev},
        {"r1", Action::page_next},   {"l2", Action::jump_prev},   {"r2", Action::jump_next},
        {"options", Action::menu},   {"touchpad", Action::touch}, {"l3", Action::l3},
        {"r3", Action::r3},
    };
    for (const auto &entry : kButtons)
        if (entry.name == button)
            return entry.action;
    return std::nullopt;
}

std::vector<Action> dev_press_actions(std::string_view argument)
{
    std::vector<Action> out;
    while (true)
    {
        const auto dash = argument.find('-');
        const auto action = dev_press_action(argument.substr(0, dash));
        if (!action || out.size() == kMaxDevPresses)
            return {};
        out.push_back(*action);
        if (dash == std::string_view::npos)
            return out;
        argument.remove_prefix(dash + 1);
    }
}

std::vector<std::uint8_t> encode_bmp(int width, int height, const std::uint8_t *rgba)
{
    if (width <= 0 || height <= 0 || rgba == nullptr)
        return {};
    const std::size_t w = static_cast<std::size_t>(width), h = static_cast<std::size_t>(height);
    const std::size_t row = (w * 3 + 3) & ~std::size_t{3};
    std::vector<std::uint8_t> out(54 + row * h, 0);
    out[0] = 'B';
    out[1] = 'M';
    put32(out, 2, static_cast<std::uint32_t>(out.size()));
    put32(out, 10, 54);
    put32(out, 14, 40);
    put32(out, 18, static_cast<std::uint32_t>(width));
    put32(out, 22, static_cast<std::uint32_t>(height));
    out[26] = 1;
    out[28] = 24;
    put32(out, 34, static_cast<std::uint32_t>(row * h));
    for (std::size_t y = 0; y < h; ++y)
    {
        const std::uint8_t *in = rgba + y * w * 4;
        std::uint8_t *line = out.data() + 54 + y * row;
        for (std::size_t x = 0; x < w; ++x)
        {
            line[x * 3] = in[x * 4 + 2];
            line[x * 3 + 1] = in[x * 4 + 1];
            line[x * 3 + 2] = in[x * 4];
        }
    }
    return out;
}

std::vector<std::string> keep_dev_screenshot(std::vector<std::string> &kept,
                                             const std::string &name)
{
    std::erase(kept, name);
    kept.push_back(name);
    std::vector<std::string> stale;
    if (kept.size() > kMaxDevScreenshots)
    {
        const auto excess = static_cast<std::ptrdiff_t>(kept.size() - kMaxDevScreenshots);
        stale.assign(kept.begin(), kept.begin() + excess);
        kept.erase(kept.begin(), kept.begin() + excess);
    }
    return stale;
}

std::string dev_status_json(const DevStatus &status)
{
    char fps[32];
    std::snprintf(fps, sizeof(fps), "%.1f", std::isfinite(status.fps) ? status.fps : 0.0f);
    return "{\"id\":" + json_quote(status.id) + ",\"version\":" + json_quote(status.version) +
           ",\"fps\":" + fps + ",\"connected\":" + (status.connected ? "true" : "false") +
           ",\"playing\":" + (status.playing ? "true" : "false") +
           ",\"display\":{\"width\":" + std::to_string(status.width) +
           ",\"height\":" + std::to_string(status.height) +
           ",\"refresh_hz\":" + std::to_string(status.refresh_hz) +
           ",\"mode\":" + std::to_string(status.display_mode) +
           "},\"page\":" + json_quote(status.page) + "}\n";
}
} // namespace hui::afterglow
