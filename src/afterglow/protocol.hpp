// Afterglow - Bounded timestamped PCM queue and Sendspin wire helpers.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace hui::afterglow
{
constexpr int kSampleRate = 48000;
constexpr std::size_t kPacketFrames = 4800;
constexpr std::size_t kPacketCount = 128;
// Advertise 2.5s, leaving packet-slot headroom for the server's 20ms chunks.
constexpr std::size_t kBufferBytes = 480000;

class AudioQueue
{
  public:
    AudioQueue();
    // One producer (network worker), one consumer (audio thread).
    bool push(const std::uint8_t *message, std::size_t bytes);
    void clear();
    bool clear_pending() const
    {
        return acknowledged_generation_.load() != generation_.load();
    }
    void render(std::int16_t *output, int frames, std::int64_t server_time_us, int volume,
                bool muted, bool active);
    double buffered_ms() const;
    std::uint64_t underruns() const
    {
        return underruns_.load();
    }
    std::uint64_t late_frames() const
    {
        return late_frames_.load();
    }
    std::uint64_t overflows() const
    {
        return overflows_.load();
    }

  private:
    struct Packet
    {
        std::array<std::int16_t, kPacketFrames * 2> pcm{};
        std::int64_t time = 0;
        std::uint64_t generation = 0;
        std::size_t frames = 0;
    };
    std::unique_ptr<Packet[]> packets_;
    std::atomic<std::size_t> read_{0}, write_{0};
    std::atomic<std::uint64_t> generation_{0}, underruns_{0}, late_frames_{0}, overflows_{0};
    std::atomic<std::uint64_t> acknowledged_generation_{0};
    std::atomic<std::int64_t> queued_frames_{0};
    std::size_t offset_ = 0;
    bool started_ = false;
    std::uint64_t consumer_generation_ = 0;
};

std::string json_quote(std::string_view value);
// JSON queries are limited to direct children, avoiding ambiguous repeated keys.
class Json
{
  public:
    explicit Json(std::string_view text);
    bool valid() const
    {
        return count_ > 0;
    }
    int get(int parent, std::string_view key) const;
    std::string string(int token) const;
    std::int64_t integer(int token, std::int64_t fallback = 0) const;
    bool boolean(int token, bool fallback = false) const;
    bool has_string(int array, std::string_view value) const;

  private:
    struct Token
    {
        int type = 0, start = 0, end = 0, parent = -1;
    };
    std::string_view text_;
    std::array<Token, 512> tokens_{};
    int count_ = 0;
};
} // namespace hui::afterglow
