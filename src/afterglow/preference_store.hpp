// Afterglow - Debounced preference persistence away from the rendering thread.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <pthread.h>
#include <string>

namespace hui::afterglow
{
class PreferenceStore
{
  public:
    // clock: microseconds; empty uses sys::monotonic_us. Tests freeze it.
    explicit PreferenceStore(std::function<std::int64_t()> clock = {});
    ~PreferenceStore();
    PreferenceStore(const PreferenceStore &) = delete;
    PreferenceStore &operator=(const PreferenceStore &) = delete;
    bool start(std::string path);
    // Plain Preferences::encode payload. The worker wraps it in the checked save container.
    // Written once nothing has changed for half a second.
    void submit(std::string preferences);
    // Written at once (within one 50 ms poll): a preset scan's checkpoint must
    // reach the disk before a kill could quarantine a preset that measured fine.
    void submit_now(std::string preferences);
    std::string status() const;
    void stop();
    // Stop then join flushes the most recently submitted payload before returning.
    void join();

  private:
    static void *entry(void *self);
    void run();
    void queue(std::string preferences, bool urgent);
    std::function<std::int64_t()> clock_;
    pthread_t thread_{};
    bool started_ = false;
    std::atomic<bool> stopping_{true};
    mutable std::mutex mutex_;
    std::string path_, pending_, status_ = "Preferences unchanged";
    std::uint64_t generation_ = 0;
    std::int64_t changed_at_ = 0;
    bool urgent_ = false;
};
} // namespace hui::afterglow
