// Afterglow - What the app and the Music over games payload say to each other.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app starts the payload by handing the console's ELF loader a copy with the server's address
// written into it, because a sandboxed app has no file the payload can read.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace hui::afterglow::music_wire
{
constexpr std::uint16_t kControlPort = 9119;
constexpr std::uint16_t kLoaderPort = 9021;
constexpr const char *kStop = "stop";
constexpr const char *kStatus = "status";
// A marker in the payload's data, followed by room for "host:port".
constexpr const char kServerMarker[] = "AFTERGLOW-MUSIC-SERVER=";
constexpr std::size_t kServerRoom = 96;

// Writes the address after the marker. False if there is no marker or the address will not fit.
inline bool embed_server(std::string &elf, const std::string &host, int port)
{
    const auto text = host + ":" + std::to_string(port);
    const std::size_t marker = sizeof kServerMarker - 1;
    const auto at = elf.find(kServerMarker);
    if (at == std::string::npos || marker + text.size() >= kServerRoom ||
        at + kServerRoom > elf.size())
        return false;
    elf.replace(at + marker, kServerRoom - marker, kServerRoom - marker, '\0');
    elf.replace(at + marker, text.size(), text);
    return true;
}

// What the payload says it is doing: the first word of its status line.
enum class PayloadState
{
    unknown,
    no_drive, // no USB drive with room
    ready,    // waiting for a press in the Music app
    waiting,  // the player is on the files, with no music coming
    playing,  // the rest of the line is the song
};
inline const char *state_word(PayloadState state)
{
    switch (state)
    {
    case PayloadState::no_drive:
        return "no-drive";
    case PayloadState::ready:
        return "ready";
    case PayloadState::waiting:
        return "waiting";
    case PayloadState::playing:
        return "playing";
    case PayloadState::unknown:
        break;
    }
    return "unknown";
}
inline PayloadState parse_state(const std::string &line)
{
    const auto word = line.substr(0, line.find(' '));
    for (const auto state : {PayloadState::no_drive, PayloadState::ready, PayloadState::waiting,
                             PayloadState::playing})
        if (word == state_word(state))
            return state;
    return PayloadState::unknown;
}
inline std::string song_in(const std::string &line)
{
    const auto space = line.find(' ');
    return space == std::string::npos ? std::string{} : line.substr(space + 1);
}
} // namespace hui::afterglow::music_wire
