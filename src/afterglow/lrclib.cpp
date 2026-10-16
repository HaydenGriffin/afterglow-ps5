// Afterglow - LRCLIB search: the request, the bounded answer and the match (LG lrclib.js).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/lrclib.hpp"
#include "afterglow/version.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hui::afterglow::lrclib
{
namespace
{
constexpr char32_t kReplacement = 0xFFFD;

// One code point from UTF-8 at text[at], advancing at; malformed bytes come
// back as U+FFFD one byte at a time, so nothing is ever lost or overread.
char32_t decode(std::string_view text, std::size_t &at)
{
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(at);
    if (lead < 0x80)
    {
        ++at;
        return lead;
    }
    const int extra = (lead >> 5) == 6 ? 1 : (lead >> 4) == 14 ? 2 : (lead >> 3) == 30 ? 3 : -1;
    if (extra < 0 || at + static_cast<std::size_t>(extra) >= text.size())
    {
        ++at;
        return kReplacement;
    }
    char32_t point = lead & (0x3F >> extra);
    for (int i = 1; i <= extra; ++i)
    {
        const unsigned char next = byte(at + static_cast<std::size_t>(i));
        if ((next & 0xC0) != 0x80)
        {
            ++at;
            return kReplacement;
        }
        point = (point << 6) | (next & 0x3F);
    }
    at += static_cast<std::size_t>(extra) + 1;
    return point;
}
void encode(char32_t point, std::string &out)
{
    if (point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF))
        point = kReplacement;
    if (point < 0x80)
        out += static_cast<char>(point);
    else if (point < 0x800)
    {
        out += static_cast<char>(0xC0 | (point >> 6));
        out += static_cast<char>(0x80 | (point & 0x3F));
    }
    else if (point < 0x10000)
    {
        out += static_cast<char>(0xE0 | (point >> 12));
        out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (point & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (point >> 18));
        out += static_cast<char>(0x80 | ((point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (point & 0x3F));
    }
}
bool even(char32_t c)
{
    return c % 2 == 0;
}
// The case pairs of the alphabets song titles use most (Latin with its
// European extensions, Greek, Cyrillic); both sides of every comparison go
// through this same table.
char32_t lower_point(char32_t c)
{
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    if (c < 0xC0)
        return c;
    if (c <= 0xDE && c != 0xD7)
        return c + 32;
    if (c == 0x178)
        return 0xFF;
    if ((c >= 0x100 && c <= 0x12F && even(c)) || (c >= 0x132 && c <= 0x137 && even(c)) ||
        (c >= 0x14A && c <= 0x177 && even(c)) || (c >= 0x139 && c <= 0x148 && !even(c)) ||
        (c >= 0x179 && c <= 0x17E && !even(c)))
        return c + 1;
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2)
        return c + 32;
    if (c == 0x386)
        return 0x3AC;
    if (c >= 0x388 && c <= 0x38A)
        return c + 37;
    if (c == 0x38C)
        return 0x3CC;
    if (c == 0x38E || c == 0x38F)
        return c + 63;
    if (c >= 0x400 && c <= 0x40F)
        return c + 80;
    if (c >= 0x410 && c <= 0x42F)
        return c + 32;
    if (((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || (c >= 0x1E00 && c <= 0x1E95) ||
         (c >= 0x1EA0 && c <= 0x1EFF)) &&
        even(c))
        return c + 1;
    return c;
}
// JavaScript's \s outside ASCII: no-break and typographic spaces.
bool space(char32_t c)
{
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F ||
           c == 0x205F || c == 0x3000 || c == 0xFEFF;
}
std::u32string points(std::string_view text)
{
    std::u32string out;
    for (std::size_t at = 0; at < text.size();)
        out += decode(text, at);
    return out;
}
std::string utf8(std::u32string_view text)
{
    std::string out;
    for (const char32_t c : text)
        encode(c, out);
    return out;
}
void trim_end(std::u32string &text)
{
    while (!text.empty() && space(text.back()))
        text.pop_back();
}
std::string encode_component(std::string_view text)
{
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') ||
            std::string_view("-_.!~*'()").find(c) != std::string_view::npos)
            out += c;
        else
        {
            out += '%';
            out += kHex[byte >> 4];
            out += kHex[byte & 15];
        }
    }
    return out;
}

// A bounded reader for LRCLIB's answer: an array of flat objects. Values it
// does not need are skipped, with nesting limited.
class Reader
{
  public:
    explicit Reader(std::string_view text) : text_(text)
    {
    }
    bool results(std::vector<Record> &records)
    {
        blank();
        if (!take('['))
            return false;
        blank();
        if (take(']'))
            return finished();
        for (;;)
        {
            Record record;
            bool keep = false;
            blank();
            if (peek() == '{')
            {
                if (!object(record))
                    return false;
                keep = true;
            }
            else if (!skip(0))
                return false;
            if (keep && records.size() < kMaxRecords)
                records.push_back(std::move(record));
            blank();
            if (take(']'))
                return finished();
            if (!take(','))
                return false;
        }
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
    bool word(std::string_view w)
    {
        if (text_.substr(at_, w.size()) != w)
            return false;
        at_ += w.size();
        return true;
    }
    void blank()
    {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\n' || text_[at_] == '\r'))
            ++at_;
    }
    bool finished()
    {
        blank();
        return at_ == text_.size();
    }
    bool hex4(char32_t &out)
    {
        if (at_ + 4 > text_.size())
            return false;
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = text_[at_++];
            const int digit = c >= '0' && c <= '9'   ? c - '0'
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                     : -1;
            if (digit < 0)
                return false;
            out = out * 16 + static_cast<char32_t>(digit);
        }
        return true;
    }
    bool string(std::string *out)
    {
        if (!take('"'))
            return false;
        while (at_ < text_.size())
        {
            const char c = text_[at_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return false;
            if (c != '\\')
            {
                if (out)
                    *out += c;
                continue;
            }
            if (at_ >= text_.size())
                return false;
            const char e = text_[at_++];
            char32_t point = 0;
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                point = static_cast<char32_t>(e);
                break;
            case 'b':
                point = '\b';
                break;
            case 'f':
                point = '\f';
                break;
            case 'n':
                point = '\n';
                break;
            case 'r':
                point = '\r';
                break;
            case 't':
                point = '\t';
                break;
            case 'u':
            {
                if (!hex4(point))
                    return false;
                // A high surrogate pairs with the low one that must follow.
                if (point >= 0xD800 && point <= 0xDBFF && word("\\u"))
                {
                    char32_t low = 0;
                    if (!hex4(low))
                        return false;
                    point = low >= 0xDC00 && low <= 0xDFFF
                                ? 0x10000 + ((point - 0xD800) << 10) + (low - 0xDC00)
                                : kReplacement;
                }
                break;
            }
            default:
                return false;
            }
            if (out)
                encode(point, *out);
        }
        return false;
    }
    bool number(double *out)
    {
        const auto start = at_;
        double value = 0;
        bool negative = take('-'), digits = false;
        while (peek() >= '0' && peek() <= '9')
        {
            value = value * 10 + (text_[at_++] - '0');
            digits = true;
        }
        if (take('.'))
        {
            double scale = 0.1;
            while (peek() >= '0' && peek() <= '9')
            {
                value += (text_[at_++] - '0') * scale;
                scale /= 10;
                digits = true;
            }
        }
        if (peek() == 'e' || peek() == 'E')
        {
            ++at_;
            const bool down = take('-');
            if (!down)
                take('+');
            int exponent = 0;
            while (peek() >= '0' && peek() <= '9')
                exponent = std::min(400, exponent * 10 + (text_[at_++] - '0'));
            value *= std::pow(10.0, down ? -exponent : exponent);
        }
        if (!digits || at_ == start)
            return false;
        if (out)
            *out = negative ? -value : value;
        return true;
    }
    bool skip(int depth)
    {
        if (depth > 32)
            return false;
        blank();
        const char c = peek();
        if (c == '"')
            return string(nullptr);
        if (c == '{' || c == '[')
        {
            const char close = c == '{' ? '}' : ']';
            ++at_;
            blank();
            if (take(close))
                return true;
            for (;;)
            {
                blank();
                if (c == '{' && !member_name(nullptr))
                    return false;
                if (!skip(depth + 1))
                    return false;
                blank();
                if (take(close))
                    return true;
                if (!take(','))
                    return false;
            }
        }
        if (c == 't')
            return word("true");
        if (c == 'f')
            return word("false");
        if (c == 'n')
            return word("null");
        return number(nullptr);
    }
    // "name": of an object member.
    bool member_name(std::string *name)
    {
        if (!string(name))
            return false;
        blank();
        return take(':');
    }
    // A string, or null for nothing; anything else is skipped as "not given".
    bool text_value(std::string &out)
    {
        blank();
        if (peek() == '"')
            return string(&out);
        return skip(1);
    }
    bool object(Record &record)
    {
        take('{');
        blank();
        if (take('}'))
            return true;
        for (;;)
        {
            blank();
            std::string key;
            if (!member_name(&key))
                return false;
            blank();
            bool ok = true;
            if (key == "trackName")
                ok = text_value(record.track);
            else if (key == "artistName")
                ok = text_value(record.artist);
            else if (key == "albumName")
                ok = text_value(record.album);
            else if (key == "syncedLyrics")
                ok = text_value(record.synced);
            else if (key == "plainLyrics")
                ok = text_value(record.plain);
            else if (key == "duration" && (peek() == '-' || (peek() >= '0' && peek() <= '9')))
                ok = number(&record.duration);
            else if (key == "instrumental" && peek() == 't')
            {
                ok = word("true");
                record.instrumental = true;
            }
            else
                ok = skip(1);
            if (!ok)
                return false;
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

std::string lower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t at = 0; at < text.size();)
    {
        const char32_t c = decode(text, at);
        // JavaScript lowers dotted capital I to "i" with a combining dot.
        if (c == 0x130)
            out += "i\xCC\x87";
        else
            encode(lower_point(c), out);
    }
    return out;
}

std::string base_title(std::string_view title)
{
    auto text = points(lower(title));
    for (auto &c : text)
        if (c == 0x2018 || c == 0x2019)
            c = '\'';
    // /\s*[([].*$/: from the whitespace before the first bracket.
    const auto bracket = text.find_first_of(U"([");
    if (bracket != std::u32string::npos)
    {
        text.resize(bracket);
        trim_end(text);
    }
    // /\s+-\s+.*$/: from the first whitespace run that is followed by a
    // dash and more whitespace.
    for (std::size_t at = 0; at < text.size(); ++at)
    {
        if (!space(text[at]) || (at > 0 && space(text[at - 1])))
            continue;
        std::size_t end = at;
        while (end < text.size() && space(text[end]))
            ++end;
        if (end + 1 < text.size() && text[end] == '-' && space(text[end + 1]))
        {
            text.resize(at);
            break;
        }
    }
    trim_end(text);
    std::size_t first = 0;
    while (first < text.size() && space(text[first]))
        ++first;
    return utf8(std::u32string_view(text).substr(first));
}

std::optional<Found> pick(const std::vector<Record> &records, const Query &query)
{
    const auto want = base_title(query.title);
    const auto who = lower(query.artist);
    const auto album = lower(query.album);
    const bool timed_song = query.duration > 0;
    std::vector<const Record *> fits;
    for (const auto &r : records)
    {
        if (r.instrumental || (r.synced.empty() && r.plain.empty()))
            continue;
        if (base_title(r.track) != want)
            continue;
        if (!who.empty() && lower(r.artist).find(who) == std::string::npos)
            continue;
        if (timed_song && r.duration > 0 && std::abs(r.duration - query.duration) > kDurationSlack)
            continue;
        fits.push_back(&r);
    }
    const auto rank = [&](const Record &r)
    {
        return (r.synced.empty() ? 0.0 : 100.0) +
               (!album.empty() && lower(r.album) == album ? 10.0 : 0.0) -
               (timed_song && r.duration > 0 ? std::abs(r.duration - query.duration)
                                             : static_cast<double>(kDurationSlack));
    };
    std::stable_sort(fits.begin(), fits.end(),
                     [&](const Record *a, const Record *b) { return rank(*a) > rank(*b); });
    if (fits.empty())
        return std::nullopt;
    return Found{fits.front()->plain, fits.front()->synced};
}

std::string search_path(const Query &query)
{
    const auto title = base_title(query.title);
    if (title.empty())
        return {};
    return "/api/search?track_name=" + encode_component(title) +
           (query.artist.empty() ? "" : "&artist_name=" + encode_component(query.artist));
}

bool parse_results(std::string_view json, std::vector<Record> &records)
{
    records.clear();
    if (json.size() > kMaxAnswer)
        return false;
    Reader reader(json);
    if (reader.results(records))
        return true;
    records.clear();
    return false;
}

std::string user_agent()
{
    return std::string("Afterglow PS5/") + kVersion +
           " (https://github.com/HaydenGriffin/afterglow-ps5)";
}
} // namespace hui::afterglow::lrclib
