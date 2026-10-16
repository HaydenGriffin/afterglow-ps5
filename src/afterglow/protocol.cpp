// Afterglow - Bounded timestamped PCM queue and Sendspin wire helpers.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/protocol.hpp"
#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#define JSMN_STRICT
#include "../../third_party/jsmn/jsmn.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>

namespace hui::afterglow
{
AudioQueue::AudioQueue() : packets_(new Packet[kPacketCount])
{
}

bool AudioQueue::push(const std::uint8_t *message, std::size_t bytes)
{
    if (!message || bytes <= 9 || message[0] != 4 || (bytes - 9) % 4 != 0)
        return false;
    std::uint64_t stamp = 0;
    for (int i = 1; i < 9; ++i)
        stamp = (stamp << 8) | message[i];
    if (stamp > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max() / 4))
        return false;
    const std::size_t frames = (bytes - 9) / 4;
    const std::size_t needed = (frames + kPacketFrames - 1) / kPacketFrames;
    std::size_t write = write_.load(std::memory_order_relaxed);
    const std::size_t read = read_.load(std::memory_order_acquire);
    const std::size_t free = (read + kPacketCount - write - 1) % kPacketCount;
    if (needed > free || frames > kBufferBytes / 4 ||
        queued_frames_.load() + static_cast<std::int64_t>(frames) >
            static_cast<std::int64_t>(kBufferBytes / 4))
    {
        ++overflows_;
        return false;
    }
    for (std::size_t position = 0; position < frames;)
    {
        Packet &packet = packets_[write];
        packet.time = static_cast<std::int64_t>(stamp) +
                      static_cast<std::int64_t>(position) * 1000000 / kSampleRate;
        packet.generation = generation_.load(std::memory_order_acquire);
        packet.frames = std::min(kPacketFrames, frames - position);
        for (std::size_t i = 0; i < packet.frames * 2; ++i)
        {
            const auto *sample = message + 9 + position * 4 + i * 2;
            const std::uint16_t bits =
                static_cast<std::uint16_t>(sample[0]) | static_cast<std::uint16_t>(sample[1] << 8);
            std::memcpy(&packet.pcm[i], &bits, sizeof(bits));
        }
        queued_frames_.fetch_add(static_cast<std::int64_t>(packet.frames));
        write = (write + 1) % kPacketCount;
        write_.store(write, std::memory_order_release);
        position += packet.frames;
    }
    return true;
}

void AudioQueue::clear()
{
    generation_.fetch_add(1, std::memory_order_release);
}

double AudioQueue::buffered_ms() const
{
    return static_cast<double>(std::max<std::int64_t>(0, queued_frames_.load())) / 48.0;
}

void AudioQueue::render(std::int16_t *output, int frames, std::int64_t server_time_us, int volume,
                        bool muted, bool active)
{
    if (!output || frames <= 0)
        return;
    std::fill_n(output, static_cast<std::size_t>(frames) * 2, std::int16_t{0});
    const auto generation = generation_.load(std::memory_order_acquire);
    if (consumer_generation_ != generation)
    {
        consumer_generation_ = generation;
        started_ = false;
    }
    int produced = 0;
    bool missing = false;
    while (produced < frames)
    {
        const std::size_t read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire))
        {
            missing = active && started_;
            break;
        }
        Packet &packet = packets_[read];
        if (packet.generation != generation)
        {
            queued_frames_.fetch_sub(static_cast<std::int64_t>(packet.frames - offset_));
            offset_ = 0;
            read_.store((read + 1) % kPacketCount, std::memory_order_release);
            continue;
        }
        if (!active)
            break;
        const std::int64_t now =
            server_time_us + static_cast<std::int64_t>(produced) * 1000000 / kSampleRate;
        const std::int64_t due =
            packet.time + static_cast<std::int64_t>(offset_) * 1000000 / kSampleRate;
        // Ignore sub-3ms scheduling jitter; larger gaps are represented by silence.
        if (due - now > 3000)
        {
            const auto remaining = static_cast<std::int64_t>(frames - produced);
            const auto silence = due - now >= remaining * 1000000 / kSampleRate
                                     ? remaining
                                     : (due - now) * kSampleRate / 1000000;
            produced += static_cast<int>(silence);
            continue;
        }
        if (now - due > 3000)
        {
            const auto remaining = packet.frames - offset_;
            const auto skip =
                now - due >= static_cast<std::int64_t>(remaining) * 1000000 / kSampleRate
                    ? remaining
                    : static_cast<std::size_t>((now - due) * kSampleRate / 1000000);
            offset_ += skip;
            late_frames_.fetch_add(skip);
            queued_frames_.fetch_sub(static_cast<std::int64_t>(skip));
        }
        const std::size_t count = std::min<std::size_t>(
            packet.frames - offset_, static_cast<std::size_t>(frames - produced));
        const int gain = muted ? 0 : std::clamp(volume, 0, 100);
        for (std::size_t i = 0; i < count * 2; ++i)
            output[static_cast<std::size_t>(produced) * 2 + i] =
                static_cast<std::int16_t>(packet.pcm[offset_ * 2 + i] * gain / 100);
        offset_ += count;
        produced += static_cast<int>(count);
        if (count > 0)
            started_ = true;
        queued_frames_.fetch_sub(static_cast<std::int64_t>(count));
        if (offset_ == packet.frames)
        {
            offset_ = 0;
            read_.store((read + 1) % kPacketCount, std::memory_order_release);
        }
    }
    if (missing)
        ++underruns_;
    acknowledged_generation_.store(generation, std::memory_order_release);
}

std::string json_quote(std::string_view value)
{
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value)
    {
        if (c == '"' || c == '\\')
        {
            result += '\\';
            result += static_cast<char>(c);
        }
        else if (c < 32)
        {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        }
        else
            result += static_cast<char>(c);
    }
    return result + '"';
}

Json::Json(std::string_view text, std::size_t max_bytes) : text_(text)
{
    if (text.size() > max_bytes ||
        text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return;
    std::array<jsmntok_t, 512> parsed{};
    jsmn_parser parser;
    jsmn_init(&parser);
    const int count = jsmn_parse(&parser, text.data(), text.size(), parsed.data(), parsed.size());
    if (count < 1 || parsed[0].type != JSMN_OBJECT)
        return;
    for (int i = 0; i < count; ++i)
    {
        if (i > 0 && parsed[static_cast<std::size_t>(i)].parent < 0)
            return;
        const auto &p = parsed[static_cast<std::size_t>(i)];
        tokens_[static_cast<std::size_t>(i)] = {p.type, p.start, p.end, p.parent};
    }
    count_ = count;
}

int Json::get(int parent, std::string_view key) const
{
    if (parent < 0 || parent >= count_ ||
        tokens_[static_cast<std::size_t>(parent)].type != JSMN_OBJECT)
        return -1;
    for (int i = parent + 1; i + 1 < count_; ++i)
    {
        const auto &token = tokens_[static_cast<std::size_t>(i)];
        if (token.parent == parent && token.type == JSMN_STRING &&
            text_.substr(static_cast<std::size_t>(token.start),
                         static_cast<std::size_t>(token.end - token.start)) == key)
            return i + 1;
    }
    return -1;
}

std::string Json::string(int token) const
{
    if (token < 0 || token >= count_ ||
        tokens_[static_cast<std::size_t>(token)].type != JSMN_STRING)
        return {};
    const auto &t = tokens_[static_cast<std::size_t>(token)];
    std::string result;
    result.reserve(static_cast<std::size_t>(t.end - t.start));
    auto hex = [](char c) -> unsigned int
    {
        if (c >= '0' && c <= '9')
            return static_cast<unsigned int>(c - '0');
        if (c >= 'a' && c <= 'f')
            return static_cast<unsigned int>(c - 'a' + 10);
        return static_cast<unsigned int>(c - 'A' + 10);
    };
    for (int i = t.start; i < t.end; ++i)
    {
        char c = text_[static_cast<std::size_t>(i)];
        if (c != '\\')
        {
            result += c;
            continue;
        }
        c = text_[static_cast<std::size_t>(++i)];
        if (c == 'u')
        {
            unsigned int cp = 0;
            for (int n = 0; n < 4; ++n)
                cp = (cp << 4) | hex(text_[static_cast<std::size_t>(++i)]);
            if (cp >= 0xd800 && cp <= 0xdbff && i + 6 < t.end &&
                text_[static_cast<std::size_t>(i + 1)] == '\\' &&
                text_[static_cast<std::size_t>(i + 2)] == 'u')
            {
                unsigned int low = 0;
                for (int n = 3; n <= 6; ++n)
                    low = (low << 4) | hex(text_[static_cast<std::size_t>(i + n)]);
                if (low >= 0xdc00 && low <= 0xdfff)
                {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                    i += 6;
                }
            }
            if (cp >= 0xd800 && cp <= 0xdfff)
                cp = 0xfffd;
            if (cp < 0x80)
                result += static_cast<char>(cp);
            else if (cp < 0x800)
            {
                result += static_cast<char>(0xc0 | (cp >> 6));
                result += static_cast<char>(0x80 | (cp & 63));
            }
            else if (cp < 0x10000)
            {
                result += static_cast<char>(0xe0 | (cp >> 12));
                result += static_cast<char>(0x80 | ((cp >> 6) & 63));
                result += static_cast<char>(0x80 | (cp & 63));
            }
            else
            {
                result += static_cast<char>(0xf0 | (cp >> 18));
                result += static_cast<char>(0x80 | ((cp >> 12) & 63));
                result += static_cast<char>(0x80 | ((cp >> 6) & 63));
                result += static_cast<char>(0x80 | (cp & 63));
            }
        }
        else
        {
            switch (c)
            {
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            default:
                break;
            }
            result += c;
        }
    }
    return result;
}

std::int64_t Json::integer(int token, std::int64_t fallback) const
{
    if (token < 0 || token >= count_ ||
        tokens_[static_cast<std::size_t>(token)].type != JSMN_PRIMITIVE)
        return fallback;
    const auto &t = tokens_[static_cast<std::size_t>(token)];
    std::int64_t value = fallback;
    const auto parsed = std::from_chars(text_.data() + t.start, text_.data() + t.end, value);
    return parsed.ec == std::errc{} && parsed.ptr == text_.data() + t.end ? value : fallback;
}

bool Json::boolean(int token, bool fallback) const
{
    if (token < 0 || token >= count_)
        return fallback;
    const auto &t = tokens_[static_cast<std::size_t>(token)];
    const auto value =
        text_.substr(static_cast<std::size_t>(t.start), static_cast<std::size_t>(t.end - t.start));
    return value == "true" ? true : value == "false" ? false : fallback;
}

bool Json::has_string(int array, std::string_view value) const
{
    if (array < 0 || array >= count_ || tokens_[static_cast<std::size_t>(array)].type != JSMN_ARRAY)
        return false;
    for (int i = array + 1; i < count_; ++i)
        if (tokens_[static_cast<std::size_t>(i)].parent == array && string(i) == value)
            return true;
    return false;
}
} // namespace hui::afterglow
