// Afterglow - Finds the USB drive to keep the song files on.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drive numbers change between sessions, so a drive is known by what is on it. Prefers the one
// that already holds the song files, since the player's list of them is kept with that drive.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace afterglow::usbflac
{
struct Drive
{
    std::string path;
    bool mounted = false, has_music = false, has_songs = false;
    std::uint64_t free_bytes = 0;
};

// The drive to use, or nothing if none is plugged in with this much room.
inline std::string pick_drive(const std::vector<Drive> &drives, std::uint64_t room_needed)
{
    const auto first = [&](bool Drive::*wanted) -> std::string
    {
        for (const auto &drive : drives)
            if (drive.mounted && drive.*wanted && drive.free_bytes >= room_needed)
                return drive.path;
        return {};
    };
    auto pick = first(&Drive::has_songs);
    if (pick.empty())
        pick = first(&Drive::has_music);
    if (pick.empty())
        pick = first(&Drive::mounted);
    return pick;
}
} // namespace afterglow::usbflac
