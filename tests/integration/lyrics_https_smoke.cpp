// Afterglow - The HTTPS client and the online lyrics search against a TLS server.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: lyrics_https_smoke get HOST PORT PATH ok|fail [STATUS] [ERROR]
//        lyrics_https_smoke lyrics HOST PORT
// The CA list comes from SSL_CERT_FILE, as on any PC.

#include "afterglow/https.hpp"
#include "afterglow/lrclib.hpp"
#include "afterglow/online_lyrics.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace hui::afterglow;

namespace
{
int get(const std::string &host, int port, const std::string &path, bool expect_ok, int status,
        const std::string &error)
{
    const auto result = https_get(
        host, path, lrclib::user_agent(), lrclib::kMaxAnswer, 8000, [] { return false; }, port);
    std::printf("get %s:%d%s -> ok=%d status=%d bytes=%zu error=%s\n", host.c_str(), port,
                path.c_str(), result.ok, result.status, result.body.size(), result.error.c_str());
    if (result.ok != expect_ok || (!error.empty() && result.error != error))
        return 1;
    return expect_ok && status != 0 && result.status != status ? 1 : 0;
}
int lyrics(const std::string &host, int port)
{
    OnlineLyrics online(
        [&](const std::string &path, const std::function<bool()> &cancelled) {
            return https_get(host, path, lrclib::user_agent(), lrclib::kMaxAnswer, 8000, cancelled,
                             port);
        });
    if (!online.start())
        return 1;
    lrclib::Query query;
    query.title = "That's It (feat. Future)";
    query.artist = "Yung Lean";
    query.duration = 161;
    online.search(1, query);
    for (int i = 0; i < 1000; ++i)
    {
        std::uint64_t ticket = 0;
        LyricsLookup::SearchOutcome outcome{};
        std::string plain, lrc;
        if (online.take_result(ticket, outcome, plain, lrc))
        {
            std::printf("lyrics -> outcome=%d lrc=%s\n", static_cast<int>(outcome), lrc.c_str());
            online.stop();
            online.join();
            return outcome == LyricsLookup::SearchOutcome::found && !lrc.empty() ? 0 : 1;
        }
        usleep(10000);
    }
    return 1;
}
} // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "get" && argc >= 6)
        return get(argv[2], std::atoi(argv[3]), argv[4], std::string(argv[5]) == "ok",
                   argc > 6 ? std::atoi(argv[6]) : 0, argc > 7 ? argv[7] : "");
    if (mode == "lyrics" && argc >= 4)
        return lyrics(argv[2], std::atoi(argv[3]));
    std::fprintf(stderr, "usage: lyrics_https_smoke get HOST PORT PATH ok|fail [STATUS] [ERROR]\n"
                         "       lyrics_https_smoke lyrics HOST PORT\n");
    return 2;
}
