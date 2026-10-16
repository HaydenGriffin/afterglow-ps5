// Afterglow - Host smoke test for metadata and durable favorite changes.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include <chrono>
#include <string>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
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
    if (argc != 2 && argc != 3)
        return 2;
    // "follow": automatic following with the relay on, as Source -> Active speaker does.
    const bool follow = argc == 3 && std::string(argv[2]) == "follow";
    hui::afterglow::Companion companion;
    companion.set_relay_enabled(follow);
    if (!follow)
        companion.select_player("speaker");
    if (!companion.start("127.0.0.1", static_cast<std::uint16_t>(std::atoi(argv[1])),
                         "fixture-token"))
        return 3;
    // The mock's LONG_PLAIN and LONG_LRC: whole lyrics, not a 2 KB prefix.
    std::string long_plain, long_lrc = "[00:01.00]Fixture\n";
    for (int i = 0; i < 200; ++i)
        long_plain += "Line " + std::to_string(i) + " of a long fixture song\n";
    for (int i = 1; i < 200; ++i)
    {
        char stamp[16];
        std::snprintf(stamp, sizeof(stamp), "[%02d:%02d.50]", i / 60, i % 60);
        long_lrc += std::string(stamp) + "<00:00.10> Word <00:00.40> été\n";
    }
    bool sent = false, metadata = false, lyrics = false, pending = false, kept = false;
    const auto started = hui::sys::monotonic_us();
    const auto deadline = started + 8000000;
    while (hui::sys::monotonic_us() < deadline)
    {
        const auto state = companion.snapshot();
        metadata |= state.track.title == "Fixture" && std::abs(state.elapsed - 12.75) < 0.01 &&
                    state.queue_id == "speaker" && !state.playing;
        lyrics |= state.track.lyrics == long_plain && state.track.lrc_lyrics == long_lrc;
        // Paused for longer than the 3 s silence and 10 s relay limits allow? Still shown.
        if (hui::sys::monotonic_us() - started > 7000000)
            kept = state.track.title == "Fixture" && state.queue_id == "speaker";
        if (state.favorites_ready && !sent)
        {
            companion.set_favorite("Native", true);
            companion.set_favorite("Old", false);
            pending = companion.pending_favorites() != "{}";
            sent = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto state = companion.snapshot();
    const bool acknowledged = companion.pending_favorites() == "{}";
    const auto begin = hui::sys::monotonic_us();
    companion.stop();
    companion.join();
    const bool stopped = hui::sys::monotonic_us() - begin < 500000;
    std::printf(
        "metadata=%d lyrics=%d journal_pending=%d acknowledged=%d bounded_stop=%d kept=%d\n",
        metadata, lyrics, pending, acknowledged, stopped, kept);
    return metadata && lyrics && pending && acknowledged && stopped && kept && !state.streaming &&
                   companion.take_pcm().empty()
               ? 0
               : 1;
}
