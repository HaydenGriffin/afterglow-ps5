// Afterglow - The idle poster: what was played, set as a poster while nothing plays.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/companion.hpp"
#include <string>
#include <vector>

// LG Afterglow's idle poster (idle-view.js). When nothing has played for a
// few seconds (half a minute after a pause, which is often a short one) the
// screen sets the paused song, or what was played recently, as a poster.
// Left and right move along the strip and Cross plays (or resumes, or opens
// Browse) on the command target; every other key keeps its usual meaning.
// On an OLED it stays calm: it steps down to a dim register after three
// minutes without a key, and the first key then only wakes it.
namespace hui::afterglow
{
constexpr float kIdleAfterSeconds = 8;
constexpr float kPausedIdleAfterSeconds = 30;
constexpr float kIdleRestSeconds = 180;
// Recent music older than this is read again when the poster next appears.
constexpr float kRecentReloadSeconds = 60;
// A start that has not taken after this long is reported as failed.
constexpr float kIdleStartSeconds = 10;

enum class IdleTileKind
{
    resume, // the paused song
    retry,  // reading recent music failed
    play,   // a recently played item
    browse, // always last, so Cross never dead-ends
    carry   // an audiobook or episode in progress, after the paused song
};
struct IdleTile
{
    IdleTileKind kind = IdleTileKind::browse;
    CompanionTrack item;
};
// The strip, in order: the paused song, up to kCarryOnLimit audiobooks and
// episodes in progress, a retry after an error, up to kRecentLimit recent
// items (none shown twice), then Browse.
constexpr std::size_t kCarryOnLimit = 3;
std::vector<IdleTile> idle_tiles(const CompanionTrack *paused,
                                 const std::vector<CompanionTrack> &recent, RecentState state,
                                 const std::vector<CompanionTrack> &in_progress = {});

// What the poster says for a tile; hint labels the Cross glyph.
struct IdleText
{
    std::string title, detail, hint;
};
IdleText idle_text(const IdleTile &tile, RecentState state, bool has_recent);

// What Cross asked for.
enum class IdleAct
{
    none,
    resume,
    play,
    browse,
    retry
};
enum class IdleKey
{
    confirm,
    left,
    right,
    other
};

// Timers, focus and the dim register; the screen draws from it.
class IdlePoster
{
  public:
    struct Inputs
    {
        bool playing = false; // the drawn queue plays
        bool paused = false;  // it is paused on a song
        bool allowed = false; // nothing else is on screen and Music Assistant answers
        RecentState recent = RecentState::idle;
        int tiles = 1; // strip length, to keep the focus on it
    };
    // Every frame. Returns true when recent music should be read now.
    bool update(float dt, const Inputs &in);
    // Any key while showing wakes the poster; true when it used the key
    // (Cross, left, right), which then does nothing else. act is what Cross
    // asked for; the first key after the dim register only wakes.
    bool press(IdleKey key, const std::vector<IdleTile> &tiles, IdleAct &act);
    // A start that was asked for: shown until the poster hides, or as a
    // failure after kIdleStartSeconds.
    void starting(const char *note);
    void failed();

    bool showing() const
    {
        return showing_;
    }
    bool resting() const
    {
        return showing_ && since_key_ >= kIdleRestSeconds;
    }
    int focus() const
    {
        return focus_;
    }
    const std::string &note() const
    {
        return note_;
    }
    // 1 awake, down to kRestLevel in the dim register; eased in update().
    float level() const
    {
        return level_;
    }
    void set_reduced_motion(bool reduced)
    {
        reduced_motion_ = reduced;
    }
    // LG #idle.rest: the whole poster at 35 % opacity.
    static constexpr float kRestLevel = 0.35f;

  private:
    void show(RecentState recent, bool &load);
    bool showing_ = false, reduced_motion_ = false, starting_ = false;
    float quiet_ = -1;      // seconds since the music stopped, -1 while it plays
    float since_key_ = 0;   // seconds since the last key while showing
    float since_load_ = -1; // seconds since recent music was asked for, -1 never
    float note_age_ = 0;
    float level_ = 1;
    int focus_ = 0;
    std::string note_;
};
} // namespace hui::afterglow
