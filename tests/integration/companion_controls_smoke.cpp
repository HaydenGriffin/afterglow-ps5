// Afterglow - Host smoke test for combined presses, token hand-over and the relay stall guard.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
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
using hui::afterglow::Command;
using hui::afterglow::Companion;
std::int64_t now()
{
    return hui::sys::monotonic_us();
}
void sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
bool wait_until(Companion &companion, std::int64_t deadline,
                bool (*ready)(const hui::afterglow::CompanionSnapshot &))
{
    while (now() < deadline)
    {
        if (ready(companion.snapshot()))
            return true;
        sleep_ms(10);
    }
    return false;
}

// Presses made faster than Music Assistant answers, then a renewed token
// handed over while the server is refusing the old one.
int controls(Companion &companion)
{
    const auto ready = [](const hui::afterglow::CompanionSnapshot &s)
    { return s.connected && s.queue_id == "leader" && s.track.title == "Fixture"; };
    if (!wait_until(companion, now() + 5000000, ready))
        return 4;
    // The first step goes out at once; the rest wait behind it and add up.
    companion.command(Command::volume_up);
    sleep_ms(50);
    for (int i = 0; i < 3; ++i)
        companion.command(Command::volume_up);
    for (int i = 0; i < 3; ++i)
        companion.seek_by(10);
    for (int i = 0; i < 5; ++i)
        companion.command(Command::next);
    companion.command(Command::toggle);
    companion.command(Command::toggle);
    const auto lost = [](const hui::afterglow::CompanionSnapshot &s) { return !s.connected; };
    if (!wait_until(companion, now() + 10000000, lost))
        return 5;
    // The worker reconnects after 3 s and offers the old token, which the
    // server refuses 2 s later; the renewal lands in between.
    sleep_ms(4000);
    companion.set_token("fresh-token");
    const auto back = [](const hui::afterglow::CompanionSnapshot &s)
    { return s.connected && !s.token_rejected; };
    if (!wait_until(companion, now() + 9000000, back))
        return 6;
    std::printf("controls: reconnected with the renewed token\n");
    return 0;
}

// Two seconds of frames, silence, a reconnect, then the speaker pauses.
int relay(Companion &companion)
{
    const auto end = now() + 28000000;
    bool streamed = false;
    while (now() < end)
    {
        streamed |= companion.snapshot().streaming;
        sleep_ms(20);
    }
    std::printf("relay: streamed=%d\n", streamed);
    return streamed ? 0 : 7;
}
} // namespace

// argv: port, "controls" or "relay". Exit code 0 = the client side passed;
// the fixture checks what reached the server.
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    const std::string mode = argv[2];
    Companion companion;
    companion.set_relay_enabled(mode == "relay");
    companion.select_player("leader");
    if (!companion.start("127.0.0.1", static_cast<std::uint16_t>(std::atoi(argv[1])),
                         "fixture-token"))
        return 3;
    const int result = mode == "controls" ? controls(companion) : relay(companion);
    companion.stop();
    companion.join();
    return result;
}
