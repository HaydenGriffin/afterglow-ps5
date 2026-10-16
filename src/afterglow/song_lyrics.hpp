// Afterglow - Song-by-song lyrics: kept timings, other lyrics and Go to a line.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/presentation.hpp"
#include "afterglow/sheet_list.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hui::afterglow
{
class OnlineLyrics;

// The lyrics features of the sheets (docs/AFTERGLOW.md, Lyrics; LG
// lyrics-tools.js). The device timing is preferences.lyrics_offset_ms; a song
// may keep its own (preferences.songs), which then replaces it.

std::int64_t epoch_ms();
// The song's identity for the store: its Music Assistant uri, "" for none.
std::string song_id(const Presentation &s);
// The timing kept for the song and the lyrics on screen, if any.
std::optional<int> kept_lyrics_timing(const Presentation &s);
// The timing that places the lyrics on screen: the kept one, else the device's.
int lyrics_offset_ms(const Presentation &s);
// Lyrics timing left/right (and L2/R2 on the Lyrics poster): the kept timing
// while there is one, else the device's. True when it changed.
bool step_lyrics_timing(Presentation &s, int direction);
bool lyrics_offer_pending(const Presentation &s);
// Every frame, wherever the lyrics are drawn: the fingerprint of the lyrics
// on screen and the keep offer's song.
void follow_song_lyrics(Presentation &s);
// Once per status tick (pump_lyrics): a picked version onto the screen, and
// the Choose other lyrics search out and back.
void pump_song_lyrics(Presentation &s, LyricsLookup &lookup, OnlineLyrics &online);

// Row tags, clear of the panel's own tags and list indexes.
enum SongLyricsRow : int
{
    keep_timing_row = 300000,
    not_now_row,
    forget_timing_row,
    choose_lyrics_row,
    go_to_line_row,
    automatic_lyrics_row,
    try_again_row,
    candidate_row = 300100,  // + candidate index
    lyric_line_row = 310000, // + line index
};
bool song_lyrics_tag(int tag);

// Settings → Lyrics, after Lyrics timing: the keep offer, or Forget timing.
void add_timing_rows(const Presentation &s, std::vector<SheetRow> &rows);
// Settings → Lyrics, after Online lyrics.
void add_choose_row(const Presentation &s, std::vector<SheetRow> &rows);
// Details, after its actions.
void add_line_row(const Presentation &s, std::vector<SheetRow> &rows);
// The Choose lyrics page.
std::vector<SheetRow> lyrics_choice_rows(const Presentation &s);
std::string lyrics_choice_note(const Presentation &s);
// The Go to a line page; focus: the row of the line on screen.
std::vector<SheetRow> lyrics_line_rows(const Presentation &s, int &focus);
std::string lyrics_lines_note(const Presentation &s);

// What a confirmed row asks of the sheet.
enum class SongLyricsStep
{
    stay,        // rows changed in place
    open_choice, // to Choose lyrics
    open_lines,  // to Go to a line
    back,        // up to the page it came from
    leave        // close the sheets: the music moved
};
SongLyricsStep confirm_song_lyrics(Presentation &s, int tag);
} // namespace hui::afterglow
