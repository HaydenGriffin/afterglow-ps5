// Afterglow - Held and quick control presses, combined instead of dropped.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace hui::afterglow
{
// A value just sent, which outranks the one Music Assistant reports until the
// report catches up or four seconds pass (LG playback.js assumedOr). Held
// volume steps build on the last value sent, not on a report still in flight.
// The key names what the value belongs to (a player, a queue item), so a value
// sent to one speaker is never carried onto another.
class Assumed
{
  public:
    static constexpr std::int64_t kFreshUs = 4000000;
    void sent(std::string key, int value, std::int64_t now_us);
    // The value to build on: the one sent to key while fresh, else actual.
    // Forgotten once Music Assistant reports it or it grows old.
    int value_or(const std::string &key, int actual, std::int64_t now_us);
    bool fresh(const std::string &key, std::int64_t now_us) const;
    int value() const
    {
        return value_;
    }
    std::int64_t sent_at() const
    {
        return at_;
    }
    void clear()
    {
        set_ = false;
    }

  private:
    bool set_ = false;
    std::string key_;
    int value_ = 0;
    std::int64_t at_ = 0;
};

enum class Control
{
    toggle,
    next,
    previous,
    volume,       // amount: percentage points, relative
    seek,         // amount: seconds, relative
    member_volume // amount: percentage points for one player of a group, relative
};
struct ControlJob
{
    Control kind = Control::toggle;
    int amount = 0;
    // member_volume: the player the steps are for. Other presses: the player
    // and queue shown as the target when pressed, so a press made just before
    // the target changes still reaches the speaker it was meant for.
    std::string player;
    std::string queue;
};

// Presses made while a command is in flight wait their turn instead of being
// lost (LG playback.js command()). Waiting volume and seek presses collapse
// into one job whose amounts add up; skips collapse into one waiting skip per
// direction, so a burst of presses never queues a burst of skips.
class ControlQueue
{
  public:
    static constexpr std::size_t kMaxWaiting = 8;
    // An unanswered command stops holding the queue after this long.
    static constexpr std::int64_t kReplyUs = 10000000;
    // False when the press was refused (the queue is full). Waiting
    // member_volume presses collapse only with those for the same player.
    bool press(Control kind, int amount = 0, const std::string &player = {},
               const std::string &queue = {});
    // The next job to send, marked in flight; nothing while one is in flight.
    std::optional<ControlJob> take(std::int64_t now_us);
    // The in-flight job was answered (or could not be sent).
    void settle();
    // Disconnected: nothing waits for a connection that is gone.
    void clear();
    bool in_flight(std::int64_t now_us) const;
    const std::deque<ControlJob> &waiting() const
    {
        return waiting_;
    }

  private:
    std::deque<ControlJob> waiting_;
    bool in_flight_ = false;
    std::int64_t sent_at_ = 0;
};
} // namespace hui::afterglow
