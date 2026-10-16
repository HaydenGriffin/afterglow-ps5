// Afterglow - When MilkDrop changes preset by itself: on a timer, each song, or on big moments.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "afterglow/preferences.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace hui::afterglow
{
// Preferences::preset_change. LG Afterglow keeps the same three, with the same
// numbers (src/preset-change.js), in its "Change preset" row.
enum class PresetChange
{
    timer = 0,
    each_song = 1,
    big_moments = 2,
};
PresetChange preset_change_of(const Preferences &preferences);

// "Every 2 min", "Each song", "On big moments".
std::string preset_change_label(const Preferences &preferences);
// One step of the "Change preset" row: the timer in 15 s steps from 15 s to
// 10 min, then Each song, then On big moments. False at either end.
bool step_preset_change(Preferences &preferences, int direction);

// What makes one song another: Music Assistant's queue item, else (no
// companion) the title and artist; empty while nothing plays.
std::string song_key(std::string_view queue_item_id, std::string_view title,
                     std::string_view artist);

// Mean square of interleaved stereo samples, full scale 1.
double pcm_energy(const float *stereo, std::size_t frames);

// Each song: a change when the song changes, not for the first song heard,
// not when the music stops. The new song must last 2 s (skipping through
// changes once, after the last) and the last preset change be 10 s back; a
// song too soon waits for the gap. Times in milliseconds.
class SongGate
{
  public:
    void song(std::string_view key, double now);
    void changed(double now)
    {
        changed_at_ = now;
    }
    bool due(double now);

  private:
    std::optional<std::string> last_;
    std::optional<double> pending_since_;
    double changed_at_ = -1e18;
};

// Big moments: the energy of the last half second against that of the last
// ten. 2.5 times (about +4 dB) is a moment, if the music is louder than
// -30 dBFS; it re-arms once the music falls below 1.25 times. Ten seconds of
// sound must come first, and three of silence (-50 dBFS) start that count
// again, so neither a song out of silence nor music resuming after a pause is
// one, while a short gap between songs keeps the level before it.
class MomentDetector
{
  public:
    bool push(double energy, double seconds);
    double fast() const
    {
        return fast_;
    }
    double slow() const
    {
        return slow_;
    }

  private:
    double fast_ = 0, slow_ = 0, heard_ = 0, silent_ = 0;
    bool armed_ = true;
};

// The two together for the mode chosen; changed() must hear of every preset
// change so the gaps hold. A moment within 25 s of a change is let go.
class PresetChanger
{
  public:
    void song(std::string_view key, double now)
    {
        songs_.song(key, now);
    }
    void hear(double energy, double seconds)
    {
        if (moments_.push(energy, seconds))
            moment_ = true;
    }
    void changed(double now)
    {
        changed_at_ = now;
        songs_.changed(now);
    }
    bool due(PresetChange mode, double now);

  private:
    SongGate songs_;
    MomentDetector moments_;
    double changed_at_ = -1e18;
    bool moment_ = false;
};
} // namespace hui::afterglow
