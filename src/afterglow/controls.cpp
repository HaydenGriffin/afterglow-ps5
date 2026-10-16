// Afterglow - Held and quick control presses, combined instead of dropped.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/controls.hpp"
#include <algorithm>
#include <utility>

namespace hui::afterglow
{
void Assumed::sent(std::string key, int value, std::int64_t now_us)
{
    set_ = true;
    key_ = std::move(key);
    value_ = value;
    at_ = now_us;
}
bool Assumed::fresh(const std::string &key, std::int64_t now_us) const
{
    return set_ && key == key_ && now_us - at_ <= kFreshUs;
}
int Assumed::value_or(const std::string &key, int actual, std::int64_t now_us)
{
    if (!fresh(key, now_us) || value_ == actual)
    {
        set_ = false;
        return actual;
    }
    return value_;
}

bool ControlQueue::press(Control kind, int amount)
{
    const auto same = std::find_if(waiting_.begin(), waiting_.end(),
                                   [kind](const ControlJob &job) { return job.kind == kind; });
    if (same != waiting_.end())
    {
        if (kind == Control::volume || kind == Control::seek)
        {
            same->amount += amount;
            return true;
        }
        // A waiting skip stands for this press too.
        if (kind == Control::next || kind == Control::previous)
            return true;
    }
    if (waiting_.size() >= kMaxWaiting)
        return false;
    waiting_.push_back({kind, amount});
    return true;
}
std::optional<ControlJob> ControlQueue::take(std::int64_t now_us)
{
    if (in_flight(now_us) || waiting_.empty())
        return std::nullopt;
    const ControlJob job = waiting_.front();
    waiting_.pop_front();
    in_flight_ = true;
    sent_at_ = now_us;
    return job;
}
void ControlQueue::settle()
{
    in_flight_ = false;
}
void ControlQueue::clear()
{
    waiting_.clear();
    in_flight_ = false;
}
bool ControlQueue::in_flight(std::int64_t now_us) const
{
    return in_flight_ && now_us - sent_at_ < kReplyUs;
}
} // namespace hui::afterglow
