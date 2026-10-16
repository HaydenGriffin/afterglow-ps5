// Afterglow - When MilkDrop changes preset by itself: on a timer, each song, or on big moments.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/preset_change.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::afterglow
{
namespace
{
constexpr int kTimerStep = 15, kTimerMin = 15, kTimerMax = 600;
constexpr double kSongSettleMs = 2000, kSongGapMs = 10000, kMomentGapMs = 25000;
constexpr double kFastSeconds = 0.5, kSlowSeconds = 10, kJump = 2.5, kRearm = 1.25;
constexpr double kLoudEnough = 1e-3, kSilence = 1e-5, kHeardSeconds = 10, kSilenceSeconds = 3;
} // namespace

PresetChange preset_change_of(const Preferences &preferences)
{
    switch (preferences.preset_change)
    {
    case 1:
        return PresetChange::each_song;
    case 2:
        return PresetChange::big_moments;
    default:
        return PresetChange::timer;
    }
}

std::string preset_change_label(const Preferences &preferences)
{
    switch (preset_change_of(preferences))
    {
    case PresetChange::each_song:
        return "Each song";
    case PresetChange::big_moments:
        return "On big moments";
    case PresetChange::timer:
        break;
    }
    const int seconds = preferences.preset_seconds;
    if (seconds < 60)
        return "Every " + std::to_string(seconds) + " s";
    const std::string minutes = "Every " + std::to_string(seconds / 60) + " min";
    return seconds % 60 == 0 ? minutes : minutes + " " + std::to_string(seconds % 60) + " s";
}

bool step_preset_change(Preferences &preferences, int direction)
{
    if (direction == 0)
        return false;
    const int mode = static_cast<int>(preset_change_of(preferences));
    if (mode > 0)
    {
        const int next = mode + (direction > 0 ? 1 : -1);
        if (next > 2)
            return false;
        preferences.preset_change = next;
        if (next == 0)
            preferences.preset_seconds = kTimerMax;
        return true;
    }
    if (direction > 0 && preferences.preset_seconds >= kTimerMax)
    {
        preferences.preset_change = 1;
        return true;
    }
    const int next =
        std::clamp(preferences.preset_seconds + (direction > 0 ? kTimerStep : -kTimerStep),
                   kTimerMin, kTimerMax);
    if (next == preferences.preset_seconds)
        return false;
    preferences.preset_seconds = next;
    return true;
}

std::string song_key(std::string_view queue_item_id, std::string_view title,
                     std::string_view artist)
{
    if (!queue_item_id.empty())
        return std::string(queue_item_id);
    if (title.empty())
        return {};
    std::string key(title);
    key += '\n';
    key += artist;
    return key;
}

double pcm_energy(const float *stereo, std::size_t frames)
{
    if (stereo == nullptr || frames == 0)
        return 0;
    double sum = 0;
    for (std::size_t i = 0; i < frames * 2; ++i)
        sum += static_cast<double>(stereo[i]) * stereo[i];
    return sum / static_cast<double>(frames * 2);
}

void SongGate::song(std::string_view key, double now)
{
    if (key.empty())
    {
        pending_since_.reset();
        return;
    }
    if (!last_ || *last_ == key)
    {
        if (last_)
            pending_since_.reset();
        last_ = std::string(key);
        return;
    }
    last_ = std::string(key);
    pending_since_ = now;
}

bool SongGate::due(double now)
{
    if (!pending_since_ || now - *pending_since_ < kSongSettleMs || now - changed_at_ < kSongGapMs)
        return false;
    pending_since_.reset();
    return true;
}

bool MomentDetector::push(double energy, double seconds)
{
    if (!(seconds > 0))
        return false;
    fast_ += (energy - fast_) * (1 - std::exp(-seconds / kFastSeconds));
    slow_ += (energy - slow_) * (1 - std::exp(-seconds / kSlowSeconds));
    if (fast_ < kSilence)
    {
        silent_ += seconds;
        if (silent_ >= kSilenceSeconds)
            heard_ = 0;
    }
    else
    {
        silent_ = 0;
        heard_ += seconds;
    }
    if (!armed_)
    {
        if (fast_ < kRearm * slow_)
            armed_ = true;
        return false;
    }
    if (heard_ < kHeardSeconds || fast_ < kLoudEnough || fast_ < kJump * slow_)
        return false;
    armed_ = false;
    return true;
}

bool PresetChanger::due(PresetChange mode, double now)
{
    const bool was_moment = std::exchange(moment_, false);
    // Drained in the other modes too, so picking Each song later does not act
    // on a song that started long before.
    const bool song_due = songs_.due(now);
    if (mode == PresetChange::each_song)
        return song_due;
    if (mode == PresetChange::big_moments)
        return was_moment && now - changed_at_ >= kMomentGapMs;
    return false;
}
} // namespace hui::afterglow
