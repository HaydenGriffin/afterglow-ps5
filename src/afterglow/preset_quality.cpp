// Afterglow - Measured preset performance and interrupted-scan recovery.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/preset_quality.hpp"
#include "afterglow/protocol.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace hui::afterglow
{
namespace
{
// Matches the preferences file's limit for this value. The packaged library
// encodes to about 27 KB, so this leaves room for long names and 1000 records.
constexpr std::size_t kMaxDocument = 262144;

bool valid_name(std::string_view name)
{
    return !name.empty() && name.size() <= 1024 && name.find_first_of("\r\n\t") == name.npos;
}
bool integer(std::string_view text, int &out)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), out);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
} // namespace

void PresetQuality::begin(const std::string &name)
{
    if (!valid_name(name))
        return;
    pending_ = name;
    warmup_ = duration_ = 0;
    frames_ = 0;
}
void PresetQuality::cancel()
{
    pending_.clear();
    warmup_ = duration_ = 0;
    frames_ = 0;
}
bool PresetQuality::observe(double frame_seconds)
{
    if (pending_.empty() || !std::isfinite(frame_seconds) || frame_seconds <= 0)
        return false;
    if (warmup_ < 0.5)
    {
        // Discard the whole boundary frame: charging a fractional frame to the
        // sample would make a slow first shader compile distort the FPS rating.
        warmup_ += frame_seconds;
        return false;
    }
    duration_ += frame_seconds;
    ++frames_;
    if (duration_ + 1e-9 < 5)
        return false;
    record(pending_, static_cast<double>(frames_) / duration_);
    return true;
}
void PresetQuality::record(const std::string &name, double fps)
{
    if (!valid_name(name) || !std::isfinite(fps) || fps < 0 || fps > 1000)
        return;
    auto &rating = ratings_[name];
    rating.fps = fps;
    rating.measured = true;
    rating.quarantined = false;
    if (name == pending_)
        pending_.clear();
}
void PresetQuality::quarantine(const std::string &name)
{
    if (!valid_name(name))
        return;
    ratings_[name].quarantined = true;
    if (name == pending_)
        pending_.clear();
}
void PresetQuality::set_keep(const std::string &name, bool keep)
{
    if (valid_name(name))
        ratings_[name].keep = keep;
}
bool PresetQuality::eligible(const std::string &name, int minimum_fps, bool skip_slow) const
{
    const auto found = ratings_.find(name);
    if (found == ratings_.end())
        return true;
    const auto &rating = found->second;
    if (rating.keep)
        return true;
    if (rating.quarantined)
        return false;
    const int threshold =
        minimum_fps == 30 || minimum_fps == 40 || minimum_fps == 45 ? minimum_fps : 60;
    return !skip_slow || !rating.measured || rating.fps + 0.5 >= threshold;
}
std::string PresetQuality::encode() const
{
    std::string result;
    for (const auto &[name, rating] : ratings_)
        result += "rating=" + std::to_string(static_cast<int>(std::lround(rating.fps * 1000))) +
                  "\t" + std::to_string(rating.measured) + "\t" +
                  std::to_string(rating.quarantined) + "\t" + std::to_string(rating.keep) + "\t" +
                  name + "\n";
    // Keep records in one escaped string: the bounded protocol parser has 512
    // tokens, while a collection can contain hundreds of independently rated presets.
    return "{\"version\":1,\"pending\":" + json_quote(pending_) +
           ",\"ratings\":" + json_quote(result) + "}";
}
PresetQuality PresetQuality::decode(std::string_view text)
{
    PresetQuality result;
    if (text.size() > kMaxDocument)
        return result;
    const Json json(text, kMaxDocument);
    if (!json.valid() || json.integer(json.get(0, "version")) != 1)
        return result;
    const std::string interrupted = json.string(json.get(0, "pending"));
    const std::string records = json.string(json.get(0, "ratings"));
    text = records;
    while (!text.empty())
    {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (!line.starts_with("rating=") || result.ratings_.size() >= 1000)
            continue;
        line.remove_prefix(7);
        int fields[4]{};
        bool valid = true;
        for (int &field : fields)
        {
            const auto tab = line.find('\t');
            if (tab == line.npos || !integer(line.substr(0, tab), field))
            {
                valid = false;
                break;
            }
            line.remove_prefix(tab + 1);
        }
        if (!valid || !valid_name(line) || fields[0] < 0 || fields[0] > 1000000 || fields[1] < 0 ||
            fields[1] > 1 || fields[2] < 0 || fields[2] > 1 || fields[3] < 0 || fields[3] > 1)
            continue;
        result.ratings_[std::string(line)] = {static_cast<double>(fields[0]) / 1000, fields[1] == 1,
                                              fields[2] == 1, fields[3] == 1};
    }
    if (!interrupted.empty())
        result.quarantine(interrupted);
    return result;
}
} // namespace hui::afterglow
