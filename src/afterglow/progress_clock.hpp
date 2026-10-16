// Afterglow - A playback position that moves every frame, not per status update.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <string>

namespace hui::afterglow
{
// Music Assistant's position reaches the screen a few times a second, so a
// progress mark drawn straight from it moves in steps. This runs its own
// clock on the frame time and leans it towards each new report:
//
// - a new report re-bases the target, which then runs on with the frames;
// - small differences (clock corrections) are eased out over about a third
//   of a second, never by moving backwards: a clock that is ahead slows down;
// - a difference of kSnapSeconds or more (a seek), a new song or the first
//   report is taken at once;
// - paused, the position holds still and only a seek moves it;
// - reduced motion follows the target directly, without the easing.
class ProgressClock
{
  public:
    static constexpr double kSnapSeconds = 1.5;

    // Once per frame. reported: the latest position (seconds) as the
    // snapshot gives it; track: anything that changes with the song (its
    // URI); duration: clamps the clock when known (> 0).
    double update(double reported, bool playing, const std::string &track, double duration,
                  float dt, bool reduced_motion);
    double position() const
    {
        return shown_;
    }

  private:
    void snap(double value);

    double shown_ = 0, target_ = 0, last_reported_ = -1;
    std::string track_;
    bool started_ = false;
};
} // namespace hui::afterglow
