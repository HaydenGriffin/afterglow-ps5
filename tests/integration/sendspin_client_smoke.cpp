// Afterglow - Host smoke test for the real native Sendspin transport.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/client.hpp"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#ifdef AFTERGLOW_TEST_DENY_FCNTL
#include <cerrno>
#include <fcntl.h>

// The test build redirects fcntl here to reproduce the observed PS5 policy.
extern "C" int afterglow_denied_fcntl(int, int command, ...)
{
    if (command == F_GETFL)
        return 0;
    errno = EACCES;
    return -1;
}
#endif

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
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    hui::afterglow::Client client;
    if (!client.start("127.0.0.1", static_cast<uint16_t>(std::atoi(argv[1])), "afterglow-test",
                      "Afterglow Test"))
        return 3;
    bool positive = false, negative = false, synced = false, metadata = false, sent = false;
    std::int16_t out[960]{};
    const auto end = hui::sys::monotonic_us() + 2200000;
    while (hui::sys::monotonic_us() < end)
    {
        client.render(out, 480);
        for (auto sample : out)
        {
            positive |= sample == 8000;
            negative |= sample == -16000;
        }
        const auto state = client.snapshot();
        synced |= state.clock_synced;
        metadata |= state.title == "Protocol test 🎵" && state.artist == "Afterglow";
        if (metadata && !sent)
        {
            client.command(hui::afterglow::Command::next);
            sent = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto begin = hui::sys::monotonic_us();
    client.stop();
    client.join();
    const auto stop_us = hui::sys::monotonic_us() - begin;
    std::printf("positive=%d negative=%d synchronized=%d metadata=%d stop_us=%lld\n", positive,
                negative, synced, metadata, static_cast<long long>(stop_us));
    return positive && negative && synced && metadata && stop_us < 500000 ? 0 : 1;
}
