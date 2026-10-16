// Afterglow - Measured preset performance and interrupted-scan recovery.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace hui::afterglow
{
struct PresetRating
{
    double fps = 0;
    bool measured = false, quarantined = false, keep = false;
};

class PresetQuality
{
  public:
    // Persist encode() before loading the named preset. An unfinished marker is
    // quarantined on decode so restarting never automatically retries a bad preset.
    void begin(const std::string &name);
    // Explicit cancellation or a clean app exit must not quarantine a healthy preset.
    void cancel();
    // One call per presented frame; discards 0.5 seconds, then measures 5 seconds.
    // Returns true exactly once when a measurement completes.
    bool observe(double frame_seconds);
    void record(const std::string &name, double fps);
    void quarantine(const std::string &name);
    void set_keep(const std::string &name, bool keep);
    bool eligible(const std::string &name, int minimum_fps, bool skip_slow) const;
    const std::map<std::string, PresetRating> &ratings() const
    {
        return ratings_;
    }
    const std::string &pending() const
    {
        return pending_;
    }
    // At most kMaxRatings records and kMaxPreferenceDocument bytes, what
    // decode() reads back: records past that are left out, never the document.
    std::string encode() const;
    // ok (when given) is false for a document that could not be read, which
    // the caller must keep rather than replace with this empty result.
    static PresetQuality decode(std::string_view text, bool *ok = nullptr);
    static constexpr std::size_t kMaxRatings = 1000;

  private:
    std::map<std::string, PresetRating> ratings_;
    std::string pending_;
    double warmup_ = 0, duration_ = 0;
    unsigned int frames_ = 0;
};
} // namespace hui::afterglow
