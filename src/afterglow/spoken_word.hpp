// Afterglow - Go to the album or artist, and audiobooks and podcasts in Details and Browse.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/companion.hpp"
#include <string>
#include <utility>
#include <vector>

// What Details (the back of the poster) offers beyond the facts: the Go to
// rows for the playing item, and Speed and Chapters while an audiobook or a
// podcast episode plays. The LG app (src/spoken-word.js, src/details-rows.js)
// keeps the same rules and words.
namespace hui::afterglow
{
// Speeds in hundredths, as left and right step through them.
constexpr int kSpeeds[] = {75, 100, 125, 150, 200};
constexpr std::size_t kMaxChapters = 300;
constexpr std::size_t kMaxArtistLinks = 3;

// Music Assistant 2.10 changes speed only for these, and resumes them itself.
bool spoken_word(const CompanionTrack &track);
// "1×", "0.75×", "1.25×", "1.5×", "2×".
std::string speed_label(int hundredths);
// Left and right stop at the ends; Cross (wrap) steps up and starts again after 2×.
int step_speed(int hundredths, int direction, bool wrap);
// The chapter playing at a position in seconds, -1 before the first.
int chapter_at(const std::vector<CompanionChapter> &chapters, double position);
// "4:05", "1:02:15".
std::string clock_label(int seconds);

// A Details row of this lane, as the panel turns it into a sheet row.
enum class DetailsRowKind
{
    go_to,
    speed,
    chapters
};
struct DetailsRow
{
    DetailsRowKind kind = DetailsRowKind::go_to;
    std::string label, value;
    // go_to: the album, artist or podcast to open (title, ids, media_type, uri).
    CompanionTrack target;
};
// Go to the album / artist (up to three) / podcast, only when Music Assistant
// gave the ids; then Speed and Chapters for spoken word with a duration.
std::vector<DetailsRow> details_rows(const CompanionTrack &track, int speed, double position);

// The Chapters page: one row per chapter, the playing one ticked.
struct ChapterRow
{
    std::string label, value;
    int start = 0;
    bool current = false;
};
std::vector<ChapterRow> chapter_rows(const CompanionTrack &track, double position);

// Radio is built from songs, not from a book, an episode or a station.
bool offers_radio(const CompanionTrack &track);
// Audiobooks and episodes have no lyrics lookup, so no Retry lyrics.
bool offers_lyrics_retry(const CompanionTrack &track);

// What an empty Browse list says for these lists; an empty title keeps the default.
std::pair<std::string, std::string> empty_list_copy(BrowseCategory category);
} // namespace hui::afterglow
