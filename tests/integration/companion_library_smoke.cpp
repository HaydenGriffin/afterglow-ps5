// Afterglow - Host smoke test for library paging, the queue window and speaker grouping.
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

using namespace hui::afterglow;
namespace
{
bool wait_for(Companion &companion, const std::function<bool(const CompanionSnapshot &)> &done,
              std::int64_t timeout_us = 4000000)
{
    const auto deadline = hui::sys::monotonic_us() + timeout_us;
    while (hui::sys::monotonic_us() < deadline)
    {
        if (done(companion.snapshot()))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    Companion companion;
    companion.set_relay_enabled(false);
    companion.select_player("speaker");
    if (!companion.start("127.0.0.1", static_cast<std::uint16_t>(std::atoi(argv[1])),
                         "fixture-token"))
        return 3;
    const bool queue = wait_for(companion,
                                [](const auto &s)
                                {
                                    return s.upcoming.size() == 50 && s.upcoming[0].title == "Q1" &&
                                           s.queue_offset == 1 && s.queue_total == 120;
                                });
    const bool badge =
        companion.snapshot().track.quality_badge == "Lossless · FLAC 44.1 kHz / 16-bit";
    const auto players = companion.snapshot().players;
    const bool grouping = players.size() == 2 && players[0].can_group_with.size() == 1;

    companion.set_queue_offset(51);
    const bool window = wait_for(companion,
                                 [](const auto &s) {
                                     return s.queue_offset == 51 && s.upcoming.size() == 50 &&
                                            s.upcoming[0].title == "Q51";
                                 });
    // The mock moves the playing song to index 5 once window 51 is asked for;
    // the next poll (4 s) must bring Up next back to the songs after it.
    const bool follows = wait_for(
        companion,
        [](const auto &s)
        {
            return s.queue_index == 5 && s.queue_offset == 6 && !s.upcoming.empty() &&
                   s.upcoming[0].title == "Q6";
        },
        7000000);

    BrowseRequest request;
    request.category = BrowseCategory::artist_albums;
    request.item_id = "426";
    request.provider = "library";
    companion.browse(request);
    const bool first = wait_for(companion,
                                [](const auto &s)
                                {
                                    return !s.browse_loading && s.browse_items.size() == 60 &&
                                           s.browse_has_more && s.browse_offset == 0;
                                });
    request.offset = 60;
    companion.browse(request);
    const bool second = wait_for(companion,
                                 [](const auto &s)
                                 {
                                     return !s.browse_loading && s.browse_items.size() == 70 &&
                                            !s.browse_has_more && s.browse_offset == 60 &&
                                            s.browse_items[60].title == "A60";
                                 });

    request = {};
    request.category = BrowseCategory::search;
    request.query = "fixture";
    companion.browse(request);
    const bool search = wait_for(companion,
                                 [](const auto &s)
                                 {
                                     return !s.browse_loading && s.browse_items.size() == 3 &&
                                            s.browse_items[0].media_type == "artist" &&
                                            s.browse_items[1].media_type == "album" &&
                                            s.browse_items[2].media_type == "track";
                                 });

    CompanionAction group;
    group.kind = CompanionActionKind::group_add;
    group.option = "speaker";
    group.text = "kitchen";
    const bool queued = companion.action(group);
    const bool grouped =
        queued && wait_for(companion, [](const auto &s)
                           { return !s.action_busy && s.action_status == "Done"; });

    companion.stop();
    companion.join();
    std::printf("queue=%d badge=%d players=%d window=%d follows=%d first=%d second=%d search=%d "
                "grouped=%d\n",
                queue, badge, grouping, window, follows, first, second, search, grouped);
    return queue && badge && grouping && window && follows && first && second && search && grouped
               ? 0
               : 1;
}
