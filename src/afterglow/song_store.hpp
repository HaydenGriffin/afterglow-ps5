// Afterglow - What is remembered about one song's lyrics: a kept timing, chosen lyrics.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/lrclib.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hui::afterglow
{
// FNV-1a (32 bits) of the text's UTF-8 without carriage returns and outer
// whitespace, as 8 hex digits; "" for no text. LG song-lyrics.js computes the same.
std::string lyrics_fingerprint(std::string_view text);

// One song in the store (LG lyricsSongs): its Music Assistant uri, the
// fingerprint of the lyrics the entry was made for, and a kept timing and/or
// the LRCLIB record chosen for it. at: epoch ms of the last change.
struct SongEntry
{
    std::string id, sum;
    std::optional<int> ms;
    std::int64_t lrclib = 0;
    std::int64_t at = 0;
    bool operator==(const SongEntry &) const = default;
};
// The store keeps the most recently changed songs; longer ids are not stored.
constexpr std::size_t kSongLimit = 100;
constexpr std::size_t kSongIdMax = 300;
using SongStore = std::vector<SongEntry>;

// The timing kept for this song and these lyrics (fingerprint sum).
std::optional<int> kept_timing(const SongStore &songs, std::string_view id, std::string_view sum);
// The LRCLIB record chosen for this song, or 0.
std::int64_t chosen_lyrics(const SongStore &songs, std::string_view id);
// Each returns whether the store changed. Keeping a timing keeps a choice
// made for the same words; choosing lyrics drops a timing kept for others.
bool keep_timing(SongStore &songs, std::string_view id, std::string_view sum, int ms,
                 std::int64_t now_ms);
bool forget_timing(SongStore &songs, std::string_view id, std::int64_t now_ms);
bool choose_lyrics(SongStore &songs, std::string_view id, std::int64_t lrclib, std::string_view sum,
                   std::int64_t now_ms);
bool clear_choice(SongStore &songs, std::string_view id, std::int64_t now_ms);

// The preference file's line value: "<at>,<ms|->,<lrclib|->,<sum>,<id>".
std::string encode_song_line(const SongEntry &entry);
bool decode_song_line(std::string_view value, SongEntry &entry);
// The account copy: a JSON array of {"id","sum","ms","lrclib","at"}, as LG
// writes it. Malformed entries are skipped; at most kSongLimit are kept.
std::string encode_songs_json(const SongStore &songs);
SongStore decode_songs_json(std::string_view json);
// Drops the oldest beyond kSongLimit (and duplicate ids, keeping the first).
void trim_songs(SongStore &songs);

// "Keep this timing for this song?": offered once per song while its device
// timing differs from what it was when the song came on (LG createTimingOffer).
class TimingOffer
{
  public:
    // Every frame: the song (its key changes with every play) and the device timing.
    void song(const std::string &key, int device_ms);
    bool pending(int device_ms, bool kept, bool timed, std::string_view id) const;
    int base() const
    {
        return base_;
    }
    void answer()
    {
        answered_ = true;
    }

  private:
    std::string key_;
    int base_ = 0;
    bool answered_ = false;
};

// Where to seek so a line at line_seconds is on screen: a timing that shows
// lyrics later by offset_ms puts the song that much further on. Whole
// seconds (player_queues/seek), from the start of the line, within 0..duration.
int line_seek_position(double line_seconds, int offset_ms, int duration);
// "3:05", "1:02:15" (shared with Chapters, defined in spoken_word.cpp).
std::string clock_label(int seconds);
// A candidate's second line: length, how far from the song's when more than
// 10 s, Timed/Plain, then the album (last, so a narrow row cuts only it).
std::string candidate_detail(const lrclib::Candidate &candidate, int duration);

// Where the lyrics on screen came from.
enum class LyricsSource
{
    none,
    music_assistant,
    lrclib,
    chosen
};
enum class CandidateState
{
    idle,
    loading,
    ready,
    error
};
// The UI's side of the song tools (Presentation::song_lyrics): what the pump
// and the screen keep current, and what the sheets ask for.
struct SongLyricsState
{
    TimingOffer offer;
    // The fingerprint of the lyrics on screen, and the text it was taken of.
    std::string sum, summed;
    LyricsSource source = LyricsSource::none;
    // Choose other lyrics: the sheet asks (candidates_wanted), the pump answers.
    bool candidates_wanted = false;
    std::uint64_t candidates_ticket = 0;
    // The song the list is for (a new song drops it).
    std::string candidates_song;
    CandidateState candidates_state = CandidateState::idle;
    std::vector<lrclib::Candidate> candidates;
    // A version the sheet picked, for the pump to put on screen.
    std::optional<lrclib::Candidate> pick;
};
} // namespace hui::afterglow
