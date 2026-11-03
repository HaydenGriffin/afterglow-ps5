// Afterglow - Uncompressed FLAC files for the console's USB music player.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from librespot-ps5 v0.1.0 connect/src/flac.rs (GPL-3.0-or-later,
// stupiditytries), changed to Afterglow's fixed 48 kHz 16-bit stereo. Every frame has a
// known size, so any moment in a file maps to a byte offset.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace afterglow::usbflac
{
constexpr std::uint32_t kRate = 48000;
constexpr std::uint32_t kBlock = 4096;
constexpr std::uint64_t kAudioStart = 8192;

inline std::uint8_t crc8(const std::uint8_t *data, std::size_t size)
{
    std::uint8_t crc = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = static_cast<std::uint8_t>(crc & 0x80 ? (crc << 1) ^ 0x07 : crc << 1);
    }
    return crc;
}

inline std::uint16_t crc16(const std::uint8_t *data, std::size_t size)
{
    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        crc = static_cast<std::uint16_t>(crc ^ (data[i] << 8));
        for (int bit = 0; bit < 8; ++bit)
            crc = static_cast<std::uint16_t>(crc & 0x8000 ? (crc << 1) ^ 0x8005 : crc << 1);
    }
    return crc;
}

inline std::uint64_t number_length(std::uint32_t index)
{
    if (index <= 0x7f)
        return 1;
    if (index <= 0x7ff)
        return 2;
    if (index <= 0xffff)
        return 3;
    if (index <= 0x1fffff)
        return 4;
    if (index <= 0x3ffffff)
        return 5;
    return 6;
}

inline void push_number(std::vector<std::uint8_t> &out, std::uint32_t index)
{
    const auto length = number_length(index);
    if (length == 1)
    {
        out.push_back(static_cast<std::uint8_t>(index));
        return;
    }
    constexpr std::uint8_t first[] = {0, 0, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc};
    out.push_back(static_cast<std::uint8_t>(first[length] | (index >> (6 * (length - 1)))));
    for (auto position = length - 1; position-- > 0;)
        out.push_back(static_cast<std::uint8_t>(0x80 | ((index >> (6 * position)) & 0x3f)));
}

inline std::uint64_t frame_offset(std::uint32_t frame)
{
    const std::uint64_t index = frame;
    const std::uint64_t base = 4 + 1 + 1 + 2 * (1 + 2 * kBlock) + 2;
    const auto between = [index](std::uint64_t low, std::uint64_t high)
    { return index < low ? 0 : (index > high ? high : index) - low; };
    return kAudioStart + index * base + between(0x80, 0x800) + 2 * between(0x800, 0x10000) +
           3 * between(0x10000, 0x200000) + 4 * between(0x200000, 0x4000000);
}

// Where the silence after the write head should reach next; `from` means no write is due. It
// goes down a whole step at a time, because topping it up frame by frame has the stick seeking
// between the silence and the audio with every frame.
inline std::uint32_t next_silence_end(std::uint32_t from, std::uint32_t head, std::uint32_t ahead,
                                      std::uint32_t step, std::uint32_t capacity)
{
    const auto wanted = head + ahead < capacity ? head + ahead : capacity;
    if (from >= wanted || (wanted - from < step && wanted < capacity))
        return from;
    return from + step < wanted ? from + step : wanted;
}

inline bool looks_like_sync(std::uint8_t first, std::uint8_t second)
{
    return first == 0xff && (second & 0xfe) == 0xf8;
}

// The player's frame search takes the first FF F8/F9 pair it finds after a seek as a real
// frame. Quiet music stores -8 and -7 as exactly those bytes, and it crashes on them, so
// samples are nudged by one step where a pair would otherwise appear.
inline void make_safe(std::int16_t *samples, std::uint32_t pairs)
{
    for (int channel = 0; channel < 2; ++channel)
        for (std::uint32_t pair = 0; pair < pairs; ++pair)
        {
            auto &slot = samples[pair * 2 + static_cast<std::uint32_t>(channel)];
            auto sample = slot;
            if (sample == -8 || sample == -7)
                sample = -6;
            if (pair + 1 < pairs)
            {
                const auto next = samples[(pair + 1) * 2 + static_cast<std::uint32_t>(channel)];
                const auto high = static_cast<std::uint8_t>(static_cast<std::uint16_t>(next) >> 8);
                if (looks_like_sync(static_cast<std::uint8_t>(sample), high))
                    --sample;
            }
            slot = sample;
        }
}

inline void push_frame_raw(std::vector<std::uint8_t> &out, std::uint32_t index,
                           const std::int16_t *samples)
{
    const auto start = out.size();
    out.insert(out.end(), {0xff, 0xf8, 0xca, 0x18}); // 4096 pairs, 48 kHz, L/R, 16-bit
    push_number(out, index);
    out.push_back(crc8(out.data() + start, out.size() - start));
    for (std::uint32_t channel = 0; channel < 2; ++channel)
    {
        out.push_back(0x02); // Verbatim subframe, no wasted bits.
        for (std::uint32_t pair = 0; pair < kBlock; ++pair)
        {
            const auto value = static_cast<std::uint16_t>(samples[pair * 2 + channel]);
            out.push_back(static_cast<std::uint8_t>(value >> 8));
            out.push_back(static_cast<std::uint8_t>(value));
        }
    }
    const auto crc = crc16(out.data() + start, out.size() - start);
    out.push_back(static_cast<std::uint8_t>(crc >> 8));
    out.push_back(static_cast<std::uint8_t>(crc));
}

// Appends one full frame. `samples` holds kBlock interleaved pairs and may be changed by a
// step or two so that the frame cannot be mistaken for the start of another.
inline void push_frame(std::vector<std::uint8_t> &out, std::uint32_t index, std::int16_t *samples)
{
    make_safe(samples, kBlock);
    const auto start = out.size();
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        out.resize(start);
        push_frame_raw(out, index, samples);
        const auto end = out.size();
        if (!looks_like_sync(out[end - 3], out[end - 2]) &&
            !looks_like_sync(out[end - 2], out[end - 1]))
            return;
        // Nudged inwards, so a sample near full scale never wraps to the other end.
        const int last = samples[kBlock * 2 - 1];
        samples[kBlock * 2 - 1] = static_cast<std::int16_t>(last > 32000 ? last - 3 : last + 3);
        make_safe(samples, kBlock);
    }
}

struct Tags
{
    std::string title, artist, album;
};

// Cut so the tags always fit the header, and never through the middle of a character.
inline std::string clipped_tag(const std::string &text, std::size_t most = 2000)
{
    if (text.size() <= most)
        return text;
    auto end = most;
    while (end > 0 && (static_cast<std::uint8_t>(text[end]) & 0xc0) == 0x80)
        --end;
    return text.substr(0, end);
}

// Everything before the first frame, padded to kAudioStart so the tags can change without moving
// the audio. The card shows the tags as the player opens the file, and takes the file's total
// time from `total_pairs`, but the player plays to the true end of the file whatever that says.
inline std::vector<std::uint8_t> header(std::uint64_t total_pairs, const Tags &tags)
{
    std::vector<std::uint8_t> out = {'f', 'L', 'a', 'C', 0, 0, 0, 34};
    for (int i = 0; i < 2; ++i)
    {
        out.push_back(static_cast<std::uint8_t>(kBlock >> 8));
        out.push_back(static_cast<std::uint8_t>(kBlock));
    }
    out.insert(out.end(), 6, 0); // Smallest and largest frame size: not stated.
    const std::uint64_t packed = (std::uint64_t{kRate} << 44) | (std::uint64_t{1} << 41) |
                                 (std::uint64_t{15} << 36) | (total_pairs & 0xfffffffffULL);
    for (int shift = 56; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(packed >> shift));
    out.insert(out.end(), 16, 0); // MD5 of the audio: not stated.

    const std::string vendor = "afterglow";
    std::vector<std::string> fields;
    const auto add_field = [&fields](const char *key, const std::string &value)
    {
        if (!value.empty())
            fields.push_back(key + clipped_tag(value));
    };
    add_field("TITLE=", tags.title);
    add_field("ARTIST=", tags.artist);
    add_field("ALBUM=", tags.album);
    std::vector<std::uint8_t> comments;
    const auto push_le = [&comments](std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            comments.push_back(static_cast<std::uint8_t>(value >> shift));
    };
    push_le(static_cast<std::uint32_t>(vendor.size()));
    comments.insert(comments.end(), vendor.begin(), vendor.end());
    push_le(static_cast<std::uint32_t>(fields.size()));
    for (const auto &field : fields)
    {
        push_le(static_cast<std::uint32_t>(field.size()));
        comments.insert(comments.end(), field.begin(), field.end());
    }
    const auto push_be24 = [&out](std::size_t value)
    {
        out.push_back(static_cast<std::uint8_t>(value >> 16));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
        out.push_back(static_cast<std::uint8_t>(value));
    };
    out.push_back(4);
    push_be24(comments.size());
    out.insert(out.end(), comments.begin(), comments.end());
    const auto padding = kAudioStart - out.size() - 4;
    out.push_back(0x80 | 1); // Padding, and the last metadata block.
    push_be24(padding);
    out.resize(kAudioStart, 0);
    return out;
}
} // namespace afterglow::usbflac
