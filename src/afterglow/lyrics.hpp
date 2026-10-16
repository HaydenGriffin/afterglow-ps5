// Afterglow - Bounded synchronized lyric parsing.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
namespace hui::afterglow
{
// One sung word of an enhanced LRC line (<mm:ss.xx> tags): when it starts and
// the byte range of text it covers, so a renderer can light it in place.
struct LyricWord
{
    double seconds = 0;
    std::size_t begin = 0, end = 0;
};
struct LyricLine
{
    double seconds = -1;
    std::string text;
    // Empty unless the line carried word timings; at most kMaxLyricWords.
    std::vector<LyricWord> words;
};
constexpr std::size_t kMaxLyricWords = 64;
std::vector<LyricLine> parse_lyrics(std::string_view text, bool timed);
int current_lyric(const std::vector<LyricLine> &lines, double elapsed);
// The last word that has started by elapsed, or -1 (none yet, or no timings).
int current_word(const LyricLine &line, double elapsed);
// Bytes of line.text sung by elapsed: through the end of the current word.
// npos when the line has no word timings (karaoke does not apply).
std::size_t sung_bytes(const LyricLine &line, double elapsed);
// How many bytes of each wrapped row are sung, given the rows wrap() made of
// text and the sung byte count. A row that cannot be found in text (an
// ellipsis row) counts as sung only when everything before it was.
std::vector<std::size_t> sung_per_row(const std::vector<std::string> &rows, std::string_view text,
                                      std::size_t sung);
} // namespace hui::afterglow
