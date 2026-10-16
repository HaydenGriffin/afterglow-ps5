// Afterglow - Host smoke test for the command target, moving music and recent music.
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
              std::int64_t timeout_us = 5000000)
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
bool idle(const CompanionSnapshot &s)
{
    return !s.action_busy && s.action_status == "Done";
}
bool takes_music(const CompanionSnapshot &s, const char *id)
{
    for (const auto &player : s.players)
        if (player.id == id)
            return player.takes_music;
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    Companion companion;
    // The relay stays shut (the fixture has none); automatic following.
    companion.set_relay_enabled(false);
    if (!companion.start("127.0.0.1", static_cast<std::uint16_t>(std::atoi(argv[1])),
                         "fixture-token"))
        return 3;

    // Nothing chosen: commands act on the queue followed, the playing living room.
    const bool following =
        wait_for(companion,
                 [](const auto &s)
                 {
                     return s.queue_id == "living" && s.target_reason == TargetReason::following &&
                            s.playing && s.players.size() == 4 && takes_music(s, "bedroom") &&
                            !takes_music(s, "kitchen") && !takes_music(s, "living");
                 });
    // A sync leader with members takes the group's volume (40 + 5).
    companion.command(Command::volume_up);
    companion.load_recent();
    const bool recent =
        wait_for(companion,
                 [](const auto &s)
                 {
                     return s.recent_state == RecentState::ready && s.recent.size() == 1 &&
                            s.recent[0].artwork.starts_with("http://127.0.0.1:") &&
                            s.recent[0].artwork.ends_with("/imageproxy/ab12?size=512&fmt=png");
                 });

    // A chosen speaker takes commands while silent: Play and Play now reach it.
    companion.select_player("bedroom");
    const bool chosen = wait_for(companion,
                                 [](const auto &s)
                                 {
                                     return s.queue_id == "bedroom" &&
                                            s.target_reason == TargetReason::chosen &&
                                            s.target_name == "Bedroom";
                                 });
    companion.command(Command::toggle);
    companion.command(Command::volume_down);
    CompanionAction play;
    play.kind = CompanionActionKind::play_media;
    play.text = "library://album/3";
    play.option = "replace";
    const bool played = companion.action(play) && wait_for(companion, idle);

    // Back to automatic: a transfer moves the living room's song to the bedroom.
    companion.select_player("");
    const bool back = wait_for(companion, [](const auto &s) { return s.queue_id == "living"; });
    CompanionAction move;
    move.kind = CompanionActionKind::transfer;
    move.text = "bedroom";
    const bool moved =
        companion.action(move) && wait_for(companion, idle) &&
        wait_for(companion,
                 [](const auto &s) {
                     return s.queue_id == "bedroom" && s.playing && s.track.title == "Fixture Song";
                 });

    // Play on this PS5: offered once its own player is known, and starts there.
    companion.set_own_player("afterglow-ps5", "ps5");
    const bool offered =
        wait_for(companion, [](const auto &s) { return s.can_move_here; }, 3000000);
    CompanionAction here;
    here.kind = CompanionActionKind::transfer;
    here.text = "ps5";
    here.enabled = true;
    const bool brought = offered && companion.action(here) && wait_for(companion, idle);

    companion.stop();
    companion.join();
    std::printf("following=%d recent=%d chosen=%d played=%d back=%d moved=%d offered=%d "
                "brought=%d\n",
                following, recent, chosen, played, back, moved, offered, brought);
    return following && recent && chosen && played && back && moved && offered && brought ? 0 : 1;
}
