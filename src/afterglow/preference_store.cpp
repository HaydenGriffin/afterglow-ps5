// Afterglow - Debounced preference persistence away from the rendering thread.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/preference_store.hpp"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"
#include <unistd.h>

namespace hui::afterglow
{
PreferenceStore::~PreferenceStore()
{
    stop();
    join();
}
bool PreferenceStore::start(std::string path)
{
    if (started_ || path.empty())
        return false;
    path_ = std::move(path);
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "Preference saving unavailable";
        return false;
    }
    stopping_ = false;
    const int stack = pthread_attr_setstacksize(&attr, 256 * 1024);
    started_ = stack == 0 && pthread_create(&thread_, &attr, entry, this) == 0;
    pthread_attr_destroy(&attr);
    if (!started_)
    {
        stopping_ = true;
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = "Preference saving unavailable";
    }
    return started_;
}
void PreferenceStore::submit(std::string preferences)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (preferences == pending_ || stopping_)
        return;
    pending_ = std::move(preferences);
    ++generation_;
    changed_at_ = sys::monotonic_us();
    status_ = "Preferences pending";
}
std::string PreferenceStore::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
void PreferenceStore::stop()
{
    stopping_ = true;
}
void PreferenceStore::join()
{
    if (started_)
    {
        pthread_join(thread_, nullptr);
        started_ = false;
    }
}
void *PreferenceStore::entry(void *self)
{
    static_cast<PreferenceStore *>(self)->run();
    return nullptr;
}
void PreferenceStore::run()
{
    std::uint64_t saved = 0;
    for (;;)
    {
        std::string text;
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (saved == generation_ && stopping_)
                return;
            if (saved != generation_ && (stopping_ || sys::monotonic_us() - changed_at_ >= 500000))
            {
                text = pending_;
                generation = generation_;
            }
        }
        if (generation == 0)
        {
            usleep(50000);
            continue;
        }
        const auto error = save::write_atomic(path_, save::encode(save::Kind::prefs, 1, text));
        saved = generation;
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == generation_)
            status_ = error.empty() ? "Preferences saved" : "Preferences not saved: " + error;
    }
}
} // namespace hui::afterglow
