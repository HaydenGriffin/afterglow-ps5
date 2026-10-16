// Afterglow - Development requests: quit, screenshot and status from a PC.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Off unless afterglow.conf has dev_requests=<token>. A PC writes one line to
// /data/homebrew/<TITLE_ID>/dev/request.txt (the app's /app0/dev/request.txt):
//
//   <verb> <argument or -> <token> <id>
//
// verb is quit, screenshot (argument: a file name), status or press
// (argument: a button, as dev_press_action names them). id names this
// request; each id is honoured once. Answers go to the title's storage
// (/download0/hui/dev), which a PC can read over FTP while the title runs.

#pragma once

#include "core/input.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hui::afterglow
{
struct DevRequest
{
    enum class Verb
    {
        quit,
        screenshot,
        status,
        press
    } verb = Verb::status;
    // The screenshot's name (no extension) or the button to press; empty for
    // the other verbs.
    std::string argument;
    std::string id;
};

// A dev_requests token is 16..128 letters, digits, '-', '_' or '.'.
bool valid_dev_token(std::string_view token);
// A screenshot name or request id: 1..48 letters, digits, '-' or '_'.
bool valid_dev_name(std::string_view name);
// The request in text (one line, trailing whitespace ignored) when it is well
// formed and carries token; nullopt otherwise. An empty token never matches.
std::optional<DevRequest> parse_dev_request(std::string_view text, std::string_view token);

// The action a press request's button stands for: up, down, left, right,
// cross, circle, triangle, square, l1, r1, l2, r2, options, touchpad, l3 or r3.
// Cross and circle are the confirm and back actions, whatever the swap setting.
std::optional<Action> dev_press_action(std::string_view button);

// Screenshots the app keeps in its storage; older ones it took are deleted.
constexpr std::size_t kMaxDevScreenshots = 8;
// Records a screenshot just saved under name (most recent last, a reused name
// moves to the end) and returns the older names to delete, past the limit.
std::vector<std::string> keep_dev_screenshot(std::vector<std::string> &kept,
                                             const std::string &name);

// A 24-bit bottom-up BMP from RGBA rows stored bottom row first, which is
// glReadPixels' order. Empty if the size is not positive.
std::vector<std::uint8_t> encode_bmp(int width, int height, const std::uint8_t *rgba);

struct DevStatus
{
    std::string id, version;
    float fps = 0;
    bool connected = false, playing = false;
    int width = 0, height = 0, refresh_hz = 0;
    int display_mode = 0;
    std::string page;
};
// One JSON object for dev/status.json.
std::string dev_status_json(const DevStatus &status);
} // namespace hui::afterglow
