// Afterglow - What is remembered about one song's lyrics: a kept timing, chosen lyrics.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/song_store.hpp"
#include "afterglow/protocol.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace hui::afterglow
{
namespace
{
constexpr int kOffsetLimitMs = 10000;

bool valid_id(std::string_view id)
{
    return !id.empty() && id.size() <= kSongIdMax && id.find_first_of("\r\n") == id.npos;
}
bool valid_sum(std::string_view sum)
{
    return sum.size() == 8 &&
           std::all_of(sum.begin(), sum.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool valid_ms(long long ms)
{
    return ms >= -kOffsetLimitMs && ms <= kOffsetLimitMs;
}
SongStore::iterator find(SongStore &songs, std::string_view id)
{
    if (!valid_id(id))
        return songs.end();
    return std::find_if(songs.begin(), songs.end(), [&](const SongEntry &e) { return e.id == id; });
}
SongStore::const_iterator find(const SongStore &songs, std::string_view id)
{
    if (!valid_id(id))
        return songs.end();
    return std::find_if(songs.begin(), songs.end(), [&](const SongEntry &e) { return e.id == id; });
}
// Moves the entry to the end (most recent), or removes it once it holds nothing.
bool settle(SongStore &songs, SongStore::iterator it, SongEntry entry, std::int64_t now_ms)
{
    if (it != songs.end() && *it == entry)
        return false;
    if (it != songs.end())
        songs.erase(it);
    if (!entry.ms && entry.lrclib == 0)
        return true;
    entry.at = now_ms;
    songs.push_back(std::move(entry));
    trim_songs(songs);
    return true;
}
template <typename T> bool whole(std::string_view text, T &out)
{
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}

// A bounded reader for the store's JSON array: flat objects of strings and
// integers. Anything else (nesting, floats) makes that entry malformed.
class Reader
{
  public:
    explicit Reader(std::string_view text) : text_(text)
    {
    }
    SongStore songs()
    {
        SongStore out;
        blank();
        if (!take('['))
            return out;
        for (int count = 0; count < static_cast<int>(kSongLimit) * 2; ++count)
        {
            blank();
            if (take(']'))
                break;
            SongEntry entry;
            const bool ok = object(entry);
            if (!ok)
                break;
            if (valid_id(entry.id) && valid_sum(entry.sum) && (entry.ms || entry.lrclib > 0))
                out.push_back(std::move(entry));
            blank();
            if (!take(','))
                break;
        }
        trim_songs(out);
        return out;
    }

  private:
    char peek() const
    {
        return at_ < text_.size() ? text_[at_] : '\0';
    }
    bool take(char c)
    {
        if (peek() != c)
            return false;
        ++at_;
        return true;
    }
    void blank()
    {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\n' || text_[at_] == '\r'))
            ++at_;
    }
    void put(std::uint32_t point, std::string &out)
    {
        if (point < 0x80)
            out += static_cast<char>(point);
        else if (point < 0x800)
        {
            out += static_cast<char>(0xC0 | (point >> 6));
            out += static_cast<char>(0x80 | (point & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xE0 | (point >> 12));
            out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (point & 0x3F));
        }
    }
    // Escapes beyond the basic plane are kept as their replacement: an id
    // with one is then simply never matched.
    bool string(std::string &out)
    {
        if (!take('"'))
            return false;
        while (at_ < text_.size() && out.size() <= kSongIdMax * 4)
        {
            const char c = text_[at_++];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            const char e = peek();
            ++at_;
            if (e == 'u')
            {
                if (at_ + 4 > text_.size())
                    return false;
                unsigned point = 0;
                const char *digits = text_.data() + at_;
                if (std::from_chars(digits, digits + 4, point, 16).ptr != digits + 4)
                    return false;
                at_ += 4;
                put(point >= 0xD800 && point <= 0xDFFF ? 0xFFFD : point, out);
            }
            else if (e == 'n')
                out += '\n';
            else if (e == 't')
                out += '\t';
            else if (e == 'r')
                out += '\r';
            else if (e == '"' || e == '\\' || e == '/')
                out += e;
            else
                return false;
        }
        return false;
    }
    bool integer(long long &out)
    {
        const auto start = at_;
        if (peek() == '-')
            ++at_;
        while (peek() >= '0' && peek() <= '9' && at_ - start < 18)
            ++at_;
        return at_ > start && whole(text_.substr(start, at_ - start), out);
    }
    bool object(SongEntry &entry)
    {
        if (!take('{'))
            return false;
        blank();
        if (take('}'))
            return true;
        for (;;)
        {
            blank();
            std::string key;
            if (!string(key))
                return false;
            blank();
            if (!take(':'))
                return false;
            blank();
            if (peek() == '"')
            {
                std::string value;
                if (!string(value))
                    return false;
                if (key == "id")
                    entry.id = std::move(value);
                else if (key == "sum")
                    entry.sum = std::move(value);
            }
            else if (peek() == 'n' && text_.substr(at_, 4) == "null")
                at_ += 4;
            else
            {
                long long n = 0;
                if (!integer(n))
                    return false;
                if (key == "ms" && valid_ms(n))
                    entry.ms = static_cast<int>(n);
                else if (key == "lrclib" && n > 0)
                    entry.lrclib = n;
                else if (key == "at" && n > 0)
                    entry.at = n;
            }
            blank();
            if (take('}'))
                return true;
            if (!take(','))
                return false;
        }
    }
    std::string_view text_;
    std::size_t at_ = 0;
};
} // namespace

std::string lyrics_fingerprint(std::string_view text)
{
    std::size_t first = 0, last = text.size();
    const auto blank = [](char c)
    { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    while (first < last && blank(text[first]))
        ++first;
    while (last > first && blank(text[last - 1]))
        --last;
    if (first == last)
        return {};
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = first; i < last; ++i)
    {
        if (text[i] == '\r')
            continue;
        hash = (hash ^ static_cast<unsigned char>(text[i])) * 0x01000193u;
    }
    char out[9];
    std::snprintf(out, sizeof out, "%08x", hash);
    return out;
}

std::optional<int> kept_timing(const SongStore &songs, std::string_view id, std::string_view sum)
{
    const auto it = find(songs, id);
    if (it == songs.end() || sum.empty() || it->sum != sum)
        return std::nullopt;
    return it->ms;
}

std::int64_t chosen_lyrics(const SongStore &songs, std::string_view id)
{
    const auto it = find(songs, id);
    return it == songs.end() ? 0 : it->lrclib;
}

bool keep_timing(SongStore &songs, std::string_view id, std::string_view sum, int ms,
                 std::int64_t now_ms)
{
    if (!valid_id(id) || !valid_sum(sum) || !valid_ms(ms))
        return false;
    const auto it = find(songs, id);
    SongEntry entry;
    entry.id = std::string(id);
    entry.sum = std::string(sum);
    entry.ms = ms;
    if (it != songs.end())
    {
        entry.at = it->at;
        // A chosen record stays chosen, unless the words are no longer its.
        if (it->sum == sum)
            entry.lrclib = it->lrclib;
    }
    return settle(songs, it, std::move(entry), now_ms);
}

bool forget_timing(SongStore &songs, std::string_view id, std::int64_t now_ms)
{
    const auto it = find(songs, id);
    if (it == songs.end() || !it->ms)
        return false;
    SongEntry entry = *it;
    entry.ms.reset();
    return settle(songs, it, std::move(entry), now_ms);
}

bool choose_lyrics(SongStore &songs, std::string_view id, std::int64_t lrclib, std::string_view sum,
                   std::int64_t now_ms)
{
    if (!valid_id(id) || !valid_sum(sum) || lrclib <= 0)
        return false;
    const auto it = find(songs, id);
    SongEntry entry;
    entry.id = std::string(id);
    entry.sum = std::string(sum);
    entry.lrclib = lrclib;
    if (it != songs.end())
        entry.at = it->at;
    return settle(songs, it, std::move(entry), now_ms);
}

bool clear_choice(SongStore &songs, std::string_view id, std::int64_t now_ms)
{
    const auto it = find(songs, id);
    if (it == songs.end() || it->lrclib == 0)
        return false;
    SongEntry entry = *it;
    entry.lrclib = 0;
    return settle(songs, it, std::move(entry), now_ms);
}

void trim_songs(SongStore &songs)
{
    SongStore unique;
    for (auto &e : songs)
        if (std::none_of(unique.begin(), unique.end(),
                         [&](const SongEntry &u) { return u.id == e.id; }))
            unique.push_back(std::move(e));
    songs = std::move(unique);
    while (songs.size() > kSongLimit)
        songs.erase(std::min_element(songs.begin(), songs.end(),
                                     [](const SongEntry &a, const SongEntry &b)
                                     { return a.at < b.at; }));
}

std::string encode_song_line(const SongEntry &entry)
{
    return std::to_string(entry.at) + "," + (entry.ms ? std::to_string(*entry.ms) : "-") + "," +
           (entry.lrclib > 0 ? std::to_string(entry.lrclib) : "-") + "," + entry.sum + "," +
           entry.id;
}

bool decode_song_line(std::string_view value, SongEntry &entry)
{
    entry = {};
    std::string_view parts[4];
    for (auto &part : parts)
    {
        const auto comma = value.find(',');
        if (comma == value.npos)
            return false;
        part = value.substr(0, comma);
        value.remove_prefix(comma + 1);
    }
    long long at = 0, ms = 0, lrclib = 0;
    if (!whole(parts[0], at) || at < 0 || !valid_sum(parts[3]) || !valid_id(value))
        return false;
    if (parts[1] != "-")
    {
        if (!whole(parts[1], ms) || !valid_ms(ms))
            return false;
        entry.ms = static_cast<int>(ms);
    }
    if (parts[2] != "-" && (!whole(parts[2], lrclib) || lrclib <= 0))
        return false;
    entry.lrclib = parts[2] == "-" ? 0 : lrclib;
    if (!entry.ms && entry.lrclib == 0)
        return false;
    entry.at = at;
    entry.sum = std::string(parts[3]);
    entry.id = std::string(value);
    return true;
}

std::string encode_songs_json(const SongStore &songs)
{
    std::string out = "[";
    for (const auto &e : songs)
    {
        if (out.size() > 1)
            out += ',';
        out += "{\"id\":" + json_quote(e.id) + ",\"sum\":" + json_quote(e.sum);
        if (e.ms)
            out += ",\"ms\":" + std::to_string(*e.ms);
        if (e.lrclib > 0)
            out += ",\"lrclib\":" + std::to_string(e.lrclib);
        out += ",\"at\":" + std::to_string(e.at) + "}";
    }
    return out + "]";
}

SongStore decode_songs_json(std::string_view json)
{
    return Reader(json).songs();
}

void TimingOffer::song(const std::string &key, int device_ms)
{
    if (key == key_)
        return;
    key_ = key;
    base_ = device_ms;
    answered_ = false;
}

bool TimingOffer::pending(int device_ms, bool kept, bool timed, std::string_view id) const
{
    return !key_.empty() && !answered_ && !kept && timed && valid_id(id) && device_ms != base_;
}

int line_seek_position(double line_seconds, int offset_ms, int duration)
{
    const int at = static_cast<int>(std::floor(line_seconds + offset_ms / 1000.0));
    return std::max(0, duration > 0 ? std::min(duration, at) : at);
}

std::string clock_label(int seconds)
{
    seconds = std::max(0, seconds);
    char out[24];
    std::snprintf(out, sizeof out, "%d:%02d", seconds / 60, seconds % 60);
    return out;
}

std::string candidate_detail(const lrclib::Candidate &c, int duration)
{
    std::string out;
    const auto add = [&out](const std::string &part)
    {
        if (!part.empty())
            out += (out.empty() ? "" : " · ") + part;
    };
    const int length = static_cast<int>(std::lround(c.duration));
    const int gap = duration > 0 && length > 0 ? length - duration : 0;
    if (length > 0)
        add(clock_label(length));
    if (std::abs(gap) > 10)
        add(clock_label(std::abs(gap)) + (gap > 0 ? " longer" : " shorter"));
    add(c.timed ? "Timed" : "Plain");
    add(c.album);
    return out;
}
} // namespace hui::afterglow
