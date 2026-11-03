// Afterglow - Starts and stops the Music over games payload, and asks how it is.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The payload runs on outside the app, so music keeps playing over a game with Afterglow closed.
// A sandboxed title cannot start it or read its files, but it can reach both the console's ELF
// loader and the payload's control port on loopback. Those talks block, so they have a thread.
#pragma once
#include "afterglow/presentation.hpp"
#include <atomic>
#include <mutex>
#include <pthread.h>
#include <string>

namespace hui::afterglow
{
class MusicOverGames
{
  public:
    MusicOverGames() = default;
    ~MusicOverGames();
    MusicOverGames(const MusicOverGames &) = delete;
    MusicOverGames &operator=(const MusicOverGames &) = delete;
    bool start();
    // Keep the payload running against this server, starting it if it is not there.
    void want_on(const std::string &host, int port);
    // Stop it.
    void want_off();
    OverGamesView view() const;

  private:
    static void *entry(void *self);
    void run();
    bool launch(const std::string &host, int port);
    void publish(OverGamesPhase phase, std::string song = {});
    mutable std::mutex mutex_;
    OverGamesView view_;
    std::string host_;
    int port_ = 0;
    bool wanted_ = false, stop_pending_ = false;
    std::atomic<bool> leaving_{false};
    pthread_t thread_{};
    bool thread_started_ = false;
};
} // namespace hui::afterglow
