// Afterglow - Host smoke test for account settings mirroring and the static delay.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
    companion.set_account_device("ps5-afterglow-ps5-test");
    companion.set_own_player("afterglow-ps5-test", "upd-ps5");
    if (!companion.start("127.0.0.1", static_cast<std::uint16_t>(std::atoi(argv[1])),
                         "fixture-token"))
        return 3;
    const bool read = until([&] { return companion.account_revision() > 0; }, 6) &&
                      companion.account_preferences() == "{\"brightness\":10}";
    const bool delay = until([&] { return companion.snapshot().static_delay_ms == 80; }, 6) &&
                       companion.snapshot().static_delay_player == "upd-ps5";
    CompanionAction action;
    action.kind = CompanionActionKind::static_delay;
    action.value = 150;
    const bool saved_delay = companion.action(action) &&
                             until([&] { return companion.snapshot().static_delay_ms == 150; }, 6);
    const auto before = hui::sys::monotonic_us();
    companion.save_account_preferences("{\"brightness\":40}");
    companion.save_account_preferences("{\"brightness\":55}");
    const bool mirrored =
        until([&] { return companion.account_preferences() == "{\"brightness\":55}"; }, 8);
    const bool debounced = hui::sys::monotonic_us() - before >= 1500000;
    companion.stop();
    companion.join();

    // A refused token is sticky: no reconnect (the worker would retry within
    // 3 s), and the flag survives the disconnect.
    Companion refused;
    refused.set_relay_enabled(false);
    const bool rejected =
        refused.start("127.0.0.1", static_cast<std::uint16_t>(std::atoi(argv[1])), "stale-token") &&
        until([&] { return refused.snapshot().token_rejected; }, 6);
    std::this_thread::sleep_for(std::chrono::milliseconds(4500));
    const auto after = refused.snapshot();
    const bool sticky = after.token_rejected && !after.connected;
    refused.stop();
    refused.join();
    std::printf("read=%d delay=%d saved_delay=%d mirrored=%d debounced=%d rejected=%d sticky=%d\n",
                read, delay, saved_delay, mirrored, debounced, rejected, sticky);
    return read && delay && saved_delay && mirrored && debounced && rejected && sticky ? 0 : 1;
}
