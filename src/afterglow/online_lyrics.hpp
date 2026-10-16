// Afterglow - LRCLIB searches on a worker thread, and the per-tick lyrics pump.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/https.hpp"
#include "afterglow/lyrics_lookup.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

namespace hui::afterglow
{
struct Presentation;

// GET of a path on lrclib.net; https_get in the app, a fake in tests.
using LyricsFetch =
    std::function<HttpsResult(const std::string &path, const std::function<bool()> &cancelled)>;
// The production fetch: https_get to lrclib.net, 8 s, kMaxAnswer bytes.
// Defined beside the HTTPS client (https.cpp), which unit tests do not link.
LyricsFetch lrclib_fetch();

// One search at a time, never on the rendering or audio threads. A newer
// search replaces one still waiting and cancels one under way.
class OnlineLyrics
{
  public:
    explicit OnlineLyrics(LyricsFetch fetch, std::function<void()> abort = {});
    ~OnlineLyrics();
    OnlineLyrics(const OnlineLyrics &) = delete;
    OnlineLyrics &operator=(const OnlineLyrics &) = delete;
    bool start();
    void search(std::uint64_t ticket, const lrclib::Query &query);
    bool take_result(std::uint64_t &ticket, LyricsLookup::SearchOutcome &outcome,
                     std::string &plain, std::string &lrc);
    // Choose other lyrics: the versions LRCLIB has for a song, asked after
    // any lookup waiting. failed: LRCLIB did not answer usefully (busy).
    void find_candidates(std::uint64_t ticket, const lrclib::Query &query);
    bool take_candidates(std::uint64_t &ticket, bool &failed,
                         std::vector<lrclib::Candidate> &candidates);
    // The last search's outcome, for the log and Status.
    std::string status() const;
    void stop();
    void join();

  private:
    static void *entry(void *self);
    void run();
    void answer(std::uint64_t ticket, const lrclib::Query &query);
    void answer_candidates(std::uint64_t ticket, const lrclib::Query &query);
    LyricsFetch fetch_;
    std::function<void()> abort_;
    mutable std::mutex mutex_;
    pthread_t thread_{};
    bool started_ = false, has_request_ = false, has_result_ = false;
    std::atomic<bool> stopping_{true};
    std::atomic<std::uint64_t> wanted_{0};
    std::uint64_t request_ticket_ = 0, result_ticket_ = 0;
    lrclib::Query request_;
    LyricsLookup::SearchOutcome outcome_ = LyricsLookup::SearchOutcome::none;
    std::string plain_, lrc_, status_ = "No online lyrics asked";
    bool has_candidates_request_ = false, has_candidates_ = false, candidates_failed_ = false;
    std::atomic<std::uint64_t> candidates_wanted_{0};
    std::uint64_t candidates_ticket_ = 0, candidates_result_ticket_ = 0;
    lrclib::Query candidates_request_;
    std::vector<lrclib::Candidate> candidates_;
};

// Once per status tick on the UI thread, after model.companion is refreshed:
// feeds the song to the lookup, runs its searches, asks Music Assistant again
// on a retry (ask_again), and puts the lyrics found into
// model.companion.track (lyrics, lrc_lyrics) and model.lyrics.
void pump_lyrics(Presentation &model, LyricsLookup &lookup, OnlineLyrics &online,
                 const std::function<void()> &ask_again, std::int64_t now_ms);
// Whether lyrics are on screen: the Lyrics poster, a lyrics layer over the
// visuals, or Settings → Lyrics. Only then is LRCLIB asked.
bool lyrics_wanted(const Presentation &model);
} // namespace hui::afterglow
