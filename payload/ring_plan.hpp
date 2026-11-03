// Afterglow - Follows the player round the ring of song files.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each song gets a file of its own, because the Music card shows a file's tags as the player
// opens it. The player goes round its folder, so a few files last for ever, and it reads 9.7 s
// ahead of what is heard, so what comes next has to be in place before it gets there.
//
// Nothing here touches a file or the clock, so all of it can be checked on a computer.
#pragma once
#include "flac_writer.hpp"
#include <cstdint>

namespace afterglow::usbflac
{
constexpr int kSlots = 32;
// How far ahead of what is heard the player reads, measured on firmware 7.20. It does not change
// with the size of the audio blocks.
constexpr double kReadAheadSeconds = 9.7;
constexpr double kShortestSeconds = 20.0;

constexpr int next_slot(int slot)
{
    return (slot + 1) % kSlots;
}
constexpr int previous_slot(int slot)
{
    return (slot + kSlots - 1) % kSlots;
}

enum class Move
{
    none,     // Still on the same file.
    started,  // The player was not on any of these files.
    onward,   // It came to the end of a file, or skipped to a song that was already there.
    next,     // Next on the card.
    previous, // Previous on the card.
    jumped,   // A file picked from the list.
};

// `from` is -1 when the player was on none of these files. `read_all` says it had read to the end
// of the file it was on, and `to_has_song` that the file it is on now holds a song: Music Assistant
// has already moved on to that one, so neither is a skip.
constexpr Move classify_move(int from, int to, bool read_all, bool to_has_song)
{
    if (from == to)
        return Move::none;
    if (from < 0)
        return Move::started;
    if (to == next_slot(from))
        return read_all || to_has_song ? Move::onward : Move::next;
    if (to == previous_slot(from))
        return Move::previous;
    return Move::jumped;
}

// How long a file should be, for a song named while its audio is already going in. The file ends
// where the song does, wherever in the file it began, so one late start does not carry over to the
// songs after it. `ahead_of_now` is how far ahead of now the audio at `into_file` is meant to be
// heard, and `song_in` how far the server says the song has got by now.
constexpr double named_file_seconds(double into_file, double ahead_of_now, double song_in,
                                    double song_length)
{
    const double song_began = into_file - ahead_of_now - song_in;
    const double seconds = song_began + song_length;
    // The server's count can be out after a seek: never end before what is already written.
    return seconds > into_file ? seconds : into_file + kShortestSeconds;
}

// Where the player has got to in the file it is on. It reads ahead and then steadily, so its read
// position gives what is heard, except near the end of a file, where the time is counted instead.
// A read position that stops short of the end means it is paused.
class PlayHead
{
  public:
    static constexpr double kStallSeconds = 3.0;
    // A press on the card can keep the player on a file this much longer than the file lasts.
    static constexpr double kOverdueSeconds = 8.0;
    // It fills its read-ahead as it opens a file, so a stall before this is not a pause.
    static constexpr double kFilledSeconds = 5.0;

    void opened(double now, std::uint64_t read_bytes)
    {
        read_ = read_bytes;
        moved_at_ = last_ = now;
        heard_ = 0;
        paused_ = false;
    }
    void update(double now, std::uint64_t read_bytes, std::uint64_t file_end, double file_seconds)
    {
        const double step = now - last_;
        last_ = now;
        const bool at_end = read_bytes >= file_end;
        if (read_bytes != read_)
        {
            read_ = read_bytes;
            moved_at_ = now;
            paused_ = false;
            const double seconds = seconds_read();
            if (seconds >= kReadAheadSeconds && !at_end)
                heard_ = seconds - kReadAheadSeconds;
            else
                heard_ += step;
            return;
        }
        if (!paused_)
            heard_ += step;
        if (paused_)
            return;
        if (at_end)
        {
            // It should have played out what it read and left the file by now.
            if (heard_ > file_seconds + kOverdueSeconds)
                paused_ = true;
        }
        else if (now - moved_at_ >= kStallSeconds && seconds_read() >= kFilledSeconds)
        {
            paused_ = true;
            const double seconds = seconds_read();
            heard_ = seconds >= kReadAheadSeconds ? seconds - kReadAheadSeconds : 0.0;
        }
    }
    double heard() const
    {
        return heard_;
    }
    bool paused() const
    {
        return paused_;
    }
    // Seconds of audio the read position has passed.
    double seconds_read() const
    {
        if (read_ <= kAudioStart)
            return 0.0;
        const double frame_bytes = static_cast<double>(frame_offset(1000) - frame_offset(999));
        return static_cast<double>(read_ - kAudioStart) / frame_bytes * kBlock / kRate;
    }

  private:
    std::uint64_t read_ = 0;
    double moved_at_ = 0, last_ = 0, heard_ = 0;
    bool paused_ = false;
};
} // namespace afterglow::usbflac
