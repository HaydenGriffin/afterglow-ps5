// Afterglow - LRCLIB search: the request, the bounded answer and the match (LG lrclib.js).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hui::afterglow::lrclib
{
// Music Assistant asks LRCLIB's exact-match endpoint with the provider's
// title; a title LRCLIB lists differently ("That's It" against "That's It
// (feat. ...)") misses, and misses are rate-limited. Search matches loosely,
// so the best hit by artist and length is taken here, as LG Afterglow does.
inline constexpr const char *kHost = "lrclib.net";
inline constexpr int kDurationSlack = 3;
// The answer for a popular title runs to several hundred kilobytes (every
// version, each with plain and synced text).
inline constexpr std::size_t kMaxAnswer = 4 * 1024 * 1024;
inline constexpr std::size_t kMaxRecords = 100;

struct Query
{
    std::string title, artist, album;
    double duration = 0; // seconds; 0 when unknown
    // Non-zero: fetch this LRCLIB record (lyrics chosen for the song,
    // song_store.hpp) instead of searching.
    std::int64_t record = 0;
};
// One search result, as much of it as the match needs.
struct Record
{
    std::string track, artist, album, synced, plain;
    double duration = 0;
    bool instrumental = false;
    std::int64_t id = 0;
};
struct Found
{
    std::string plain, lrc;
};

// Lower case the way JavaScript's toLowerCase does for Latin, Greek and
// Cyrillic letters; other bytes are kept.
std::string lower(std::string_view text);
// "That's It (feat. Future) [from GTA VI]" -> "that's it"; "Song - 2011 Remaster" -> "song".
std::string base_title(std::string_view title);
// The hit to use, or nothing: same base title, the artist named, the length
// within a few seconds when both are known; timed lyrics before plain ones,
// then the same album, then the closer length.
std::optional<Found> pick(const std::vector<Record> &records, const Query &query);
// "/api/search?track_name=...&artist_name=..." (encodeURIComponent), or ""
// when the title has nothing to search for.
std::string search_path(const Query &query);
// A JSON array of search results. False when the text is not one (a broken
// or cut-off answer is a failure, not "no lyrics").
bool parse_results(std::string_view json, std::vector<Record> &records);
// "/api/get/<id>": one record by its id, or "" for no id.
std::string get_path(std::int64_t id);
// One JSON object, as /api/get answers. False when the text is not one.
bool parse_record(std::string_view json, Record &record);

// "Choose other lyrics" (LG lrclib.js lyricCandidates): a version to pick.
struct Candidate
{
    std::int64_t id = 0;
    std::string title, artist, album, plain, lrc;
    // The fingerprint of its lyric text (song_store.hpp).
    std::string sum;
    double duration = 0;
    bool timed = false;
    // Same base title, and the artist named.
    bool same = false;
};
inline constexpr std::size_t kCandidateLimit = 6;
// Up to kCandidateLimit versions: with an id and lyrics, not instrumental, one
// per distinct lyric text; the same title by the artist first, then the
// closest length (unknown last), then timed before plain. None is dropped for
// its length: a much longer (live) version is listed and says so.
std::vector<Candidate> candidates(const std::vector<Record> &records, const Query &query);

// "Afterglow PS5/<version> (https://github.com/HaydenGriffin/afterglow-ps5)"
std::string user_agent();
} // namespace hui::afterglow::lrclib
