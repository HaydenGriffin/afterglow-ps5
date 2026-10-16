// Afterglow - Bounded synchronized lyric parsing.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/lyrics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <charconv>
namespace hui::afterglow
{
namespace
{
bool word_time(std::string_view tag, double &out)
{
    const std::string text(tag);
    int minutes = 0;
    double seconds = 0;
    char extra = 0;
    if (std::sscanf(text.c_str(), "%d:%lf%c", &minutes, &seconds, &extra) != 2 || minutes < 0 ||
        !std::isfinite(seconds) || seconds < 0 || seconds >= 60)
        return false;
    out = static_cast<double>(minutes) * 60 + seconds;
    return true;
}
struct Stripped
{
    std::string text;
    std::vector<LyricWord> words;
};
// Enhanced LRC word tags carry timing, not printable lyrics: they are removed
// from the text and kept as each word's start and byte range.
Stripped strip_word_tags(std::string_view raw)
{
    Stripped out;
    double pending = -1;
    std::size_t begin = 0;
    const auto flush = [&]
    {
        if (pending < 0)
            return;
        std::size_t first = begin, last = out.text.size();
        while (first < last && out.text[first] == ' ')
            ++first;
        while (last > first && out.text[last - 1] == ' ')
            --last;
        if (first < last && out.words.size() < kMaxLyricWords)
            out.words.push_back({pending, first, last});
        pending = -1;
    };
    for (std::size_t at = 0; at < raw.size();)
    {
        double time = 0;
        const auto close = raw[at] == '<' ? raw.find('>', at) : raw.npos;
        if (close != raw.npos && word_time(raw.substr(at + 1, close - at - 1), time))
        {
            flush();
            pending = time;
            begin = out.text.size();
            at = close + 1;
            continue;
        }
        out.text += raw[at++];
    }
    flush();
    return out;
}
} // namespace

std::vector<LyricLine> parse_lyrics(std::string_view text, bool timed)
{
    std::vector<LyricLine> result;
    text = text.substr(0, 65536);
    double offset = 0;
    if (timed)
    {
        const auto begin = text.find("[offset:");
        if (begin != text.npos)
        {
            const auto end = text.find(']', begin);
            auto value = text.substr(begin + 8, end == text.npos ? 0 : end - begin - 8);
            if (!value.empty() && value.front() == '+')
                value.remove_prefix(1);
            int milliseconds = 0;
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(), milliseconds);
            if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size())
                offset = std::clamp(milliseconds, -600000, 600000) / 1000.0;
        }
    }
    while (!text.empty() && result.size() < 500)
    {
        const auto end = text.find('\n');
        std::string line(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!timed)
        {
            if (!line.empty())
            {
                LyricLine plain;
                plain.text = line;
                result.push_back(std::move(plain));
            }
            continue;
        }
        std::vector<double> times;
        std::size_t start = 0;
        while (start < line.size() && line[start] == '[' && times.size() < 16)
        {
            const auto close = line.find(']', start);
            if (close == line.npos)
                break;
            const std::string stamp = line.substr(start + 1, close - start - 1);
            int minutes = 0;
            double seconds = 0;
            char extra = 0;
            if (std::sscanf(stamp.c_str(), "%d:%lf%c", &minutes, &seconds, &extra) != 2 ||
                minutes < 0 || minutes > 10000 || !std::isfinite(seconds) || seconds < 0 ||
                seconds >= 60)
                break;
            times.push_back(static_cast<double>(minutes) * 60 + seconds);
            start = close + 1;
        }
        const Stripped words = strip_word_tags(std::string_view(line).substr(start));
        for (double time : times)
            if (result.size() < 500)
            {
                LyricLine out{std::max(0.0, time + offset), words.text, words.words};
                // A repeated line ([00:10][01:10]) replays its word timings
                // shifted to each occurrence.
                for (auto &word : out.words)
                    word.seconds = std::max(0.0, word.seconds + (time - times.front()) + offset);
                result.push_back(std::move(out));
            }
    }
    if (timed)
        std::stable_sort(result.begin(), result.end(),
                         [](const auto &a, const auto &b) { return a.seconds < b.seconds; });
    return result;
}
int current_lyric(const std::vector<LyricLine> &lines, double elapsed)
{
    int active = -1;
    for (std::size_t i = 0; i < lines.size(); ++i)
        if (lines[i].seconds >= 0 && lines[i].seconds <= elapsed)
            active = static_cast<int>(i);
    return active;
}
int current_word(const LyricLine &line, double elapsed)
{
    int active = -1;
    for (std::size_t i = 0; i < line.words.size(); ++i)
        if (line.words[i].seconds <= elapsed)
            active = static_cast<int>(i);
    return active;
}
std::size_t sung_bytes(const LyricLine &line, double elapsed)
{
    if (line.words.empty())
        return std::string::npos;
    const int word = current_word(line, elapsed);
    return word < 0 ? 0
                    : std::min(line.words[static_cast<std::size_t>(word)].end, line.text.size());
}
std::vector<std::size_t> sung_per_row(const std::vector<std::string> &rows, std::string_view text,
                                      std::size_t sung)
{
    constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
    std::vector<std::size_t> out;
    std::size_t cursor = 0;
    for (const auto &row : rows)
    {
        std::string_view body = row;
        if (body.ends_with(kEllipsis))
            body.remove_suffix(kEllipsis.size());
        while (!body.empty() && body.back() == ' ')
            body.remove_suffix(1);
        const auto at = body.empty() ? text.npos : text.find(body, cursor);
        if (at == text.npos)
        {
            out.push_back(sung >= text.size() ? row.size() : 0);
            continue;
        }
        const std::size_t lit = sung <= at ? 0 : std::min(sung - at, body.size());
        out.push_back(lit == body.size() && sung >= text.size() ? row.size() : lit);
        cursor = at + body.size();
    }
    return out;
}
} // namespace hui::afterglow
