// Afterglow - A playback position that moves every frame, not per status update.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/progress_clock.hpp"
#include <algorithm>
#include <cmath>

namespace hui::afterglow
{
namespace
{
// Corrections ease out at this rate per second: 86 % gone after a third of a second.
constexpr double kEaseRate = 6;
// Reports arrive about ten times a second while the music plays. When they
// stop moving (a stall, a server that went quiet) the clock runs on past the
// last one for at most this long rather than inventing the rest of the song.
constexpr double kRunOnSeconds = 1;
} // namespace

void ProgressClock::snap(double value)
{
    shown_ = target_ = value;
}

double ProgressClock::update(double reported, bool playing, const std::string &track,
                             double duration, float dt, bool reduced_motion)
{
    reported = std::max(0.0, reported);
    const double step = std::max(0.0, static_cast<double>(dt));
    if (!started_ || track != track_)
    {
        started_ = true;
        track_ = track;
        last_reported_ = reported;
        snap(reported);
    }
    else
    {
        const double before = shown_;
        // How far the music moved this frame as far as the clock can tell:
        // a frame's time while it plays, less once reports have gone quiet.
        double advance = playing ? step : 0;
        if (reported != last_reported_)
        {
            last_reported_ = reported;
            target_ = reported;
        }
        else if (playing)
        {
            const double was = target_;
            target_ = std::min(target_ + step, last_reported_ + kRunOnSeconds);
            advance = std::max(0.0, target_ - was);
        }
        const double error = target_ - (shown_ + advance);
        if (std::abs(error) >= kSnapSeconds)
            snap(target_);
        else if (playing)
        {
            const double ease = reduced_motion ? 1 : 1 - std::exp(-step * kEaseRate);
            // Ahead of the music: slow down or wait, never step back.
            shown_ = std::max(before, shown_ + advance + error * ease);
        }
    }
    if (duration > 0)
    {
        shown_ = std::min(shown_, duration);
        target_ = std::min(target_, duration);
    }
    return shown_;
}
} // namespace hui::afterglow
