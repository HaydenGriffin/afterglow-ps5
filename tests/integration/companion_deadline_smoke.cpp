// Afterglow - Host smoke test for requests Music Assistant never answers.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>

namespace hui::sys
{
void log(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}
std::int64_t monotonic_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace hui::sys

namespace
{
bool until(const std::function<bool()> &condition, int seconds)
{
    const auto deadline = hui::sys::monotonic_us() + seconds * 1000000LL;
    while (hui::sys::monotonic_us() < deadline)
    {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    using namespace hui::afterglow;
    if (argc != 2)
        return 2;
    Companion companion;
    companion.set_relay_enabled(false);
    if (!companion.start("127.0.0.1", static_cast<std::uint16_t>(std::atoi(argv[1])),
                         "fixture-token"))
        return 3;
    const auto started = hui::sys::monotonic_us();
    if (!until([&] { return companion.snapshot().connected; }, 5))
        return 4;
    // The first recent read is never answered: it must end as an error, not
    // stay loading, and a second request must then go out.
    companion.load_recent();
    const bool recent_failed =
        until([&] { return companion.snapshot().recent_state == RecentState::error; }, 15);
    companion.load_recent();
    const bool recent_ready = until(
        [&]
        {
            const auto state = companion.snapshot();
            return state.recent_state == RecentState::ready && state.recent.size() == 1;
        },
        5);
    // The first account read is never answered: favourites arrive from the retry.
    const bool account_read = until([&] { return companion.snapshot().favorites_ready; }, 20);
    const bool waited = hui::sys::monotonic_us() - started >= 9000000;
    // The first save is answered only late, the second refused: the edit stays
    // pending through both and is acknowledged by the third.
    companion.set_favorite("Native", true);
    const bool pending = companion.pending_favorites() != "{}";
    const bool held = !until([&] { return companion.pending_favorites() == "{}"; }, 12);
    const bool saved = until([&] { return companion.pending_favorites() == "{}"; }, 25);
    const bool connected = companion.snapshot().connected;
    companion.stop();
    companion.join();
    std::printf("recent_failed=%d recent_ready=%d account_read=%d waited=%d pending=%d held=%d "
                "saved=%d connected=%d\n",
                recent_failed, recent_ready, account_read, waited, pending, held, saved, connected);
    return recent_failed && recent_ready && account_read && waited && pending && held && saved &&
                   connected
               ? 0
               : 1;
}
