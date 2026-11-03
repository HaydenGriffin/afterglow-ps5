// Afterglow - Host checks for the Music over games payload's platform-neutral parts.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../payload/drive_finder.hpp"
#include "../../payload/file_writer.hpp"
#include "../../payload/flac_writer.hpp"
#include "../../payload/now_playing.hpp"
#include "../../payload/ring_plan.hpp"
#include "afterglow/music_over_games_wire.hpp"
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

using namespace afterglow::usbflac;

namespace
{
std::uint64_t claimed_pairs(const std::vector<std::uint8_t> &head)
{
    std::uint64_t packed = 0;
    for (int i = 0; i < 8; ++i)
        packed = packed << 8 | head[18 + static_cast<std::size_t>(i)];
    return packed & 0xfffffffffULL;
}

std::string as_text(const std::vector<std::uint8_t> &bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

TEST(MusicFlac, HeaderCarriesTagsAndLengthWithoutMovingTheAudio)
{
    const auto head = header(48000 * 200, Tags{"Song", "Singer", "Album"});
    ASSERT_EQ(head.size(), kAudioStart);
    EXPECT_EQ(claimed_pairs(head), 48000u * 200);
    const auto text = as_text(head);
    EXPECT_NE(text.find("TITLE=Song"), std::string::npos);
    EXPECT_NE(text.find("ARTIST=Singer"), std::string::npos);
    EXPECT_NE(text.find("ALBUM=Album"), std::string::npos);
}

TEST(MusicFlac, EmptyTagsAreLeftOut)
{
    const auto text = as_text(header(1, Tags{"Only", "", ""}));
    EXPECT_NE(text.find("TITLE=Only"), std::string::npos);
    EXPECT_EQ(text.find("ARTIST="), std::string::npos);
    EXPECT_EQ(text.find("ALBUM="), std::string::npos);
}

TEST(MusicFlac, HugeTagsStillFitTheHeader)
{
    const std::string long_text(50000, 'x');
    EXPECT_EQ(header(1, Tags{long_text, long_text, long_text}).size(), kAudioStart);
}

TEST(MusicFlac, ClippingNeverSplitsACharacter)
{
    std::string text;
    for (int i = 0; i < 1500; ++i)
        text += "\xc3\xa9";
    const auto clipped = clipped_tag(text, 1001);
    EXPECT_EQ(clipped.size(), 1000u);
    EXPECT_EQ(static_cast<unsigned char>(clipped.back()), 0xa9);
    EXPECT_EQ(clipped_tag("short", 1001), "short");
}

TEST(MusicFlac, SilenceStepsAreWholeSteps)
{
    EXPECT_EQ(next_silence_end(100, 100, 300, 60, 100000), 160u);
    EXPECT_EQ(next_silence_end(380, 100, 300, 60, 100000), 380u)
        << "less than a step short: not yet";
    EXPECT_EQ(next_silence_end(400, 100, 300, 60, 100000), 400u);
    EXPECT_EQ(next_silence_end(990, 900, 300, 60, 1000), 1000u) << "the last step may be short";
    EXPECT_EQ(next_silence_end(1000, 900, 300, 60, 1000), 1000u);
}

struct Landed
{
    int fd;
    bool cut;
    std::uint64_t offset;
    std::size_t size;
};
std::vector<Landed> g_landed;
ssize_t fake_write(int fd, const void *, std::size_t size, off_t offset)
{
    g_landed.push_back({fd, false, static_cast<std::uint64_t>(offset), size});
    return static_cast<ssize_t>(size);
}
int fake_cut(int fd, off_t length)
{
    g_landed.push_back({fd, true, static_cast<std::uint64_t>(length), 0});
    return 0;
}
int failing_cut(int, off_t)
{
    errno = EIO;
    return -1;
}

TEST(MusicWriter, WritesAndCutsLandInOrderAcrossFiles)
{
    g_landed.clear();
    FileWriter writer(fake_write, fake_cut);
    ASSERT_TRUE(writer.start());
    const std::vector<std::uint8_t> bytes(100, 1);
    ASSERT_TRUE(writer.queue(3, 0, bytes.data(), bytes.size()));
    ASSERT_TRUE(writer.queue(3, 100, bytes.data(), bytes.size()));
    ASSERT_TRUE(writer.queue_cut(3, 150));
    ASSERT_TRUE(writer.queue(4, 0, bytes.data(), bytes.size()));
    writer.flush();
    writer.stop();
    ASSERT_EQ(g_landed.size(), 3u);
    EXPECT_EQ(g_landed[0].fd, 3);
    EXPECT_EQ(g_landed[0].size, 200u) << "neighbours join";
    EXPECT_TRUE(g_landed[1].cut);
    EXPECT_EQ(g_landed[1].offset, 150u);
    EXPECT_EQ(g_landed[2].fd, 4);
    EXPECT_EQ(g_landed[2].size, 100u);
}

TEST(MusicWriter, NeighbouringOffsetsInAnotherFileDoNotJoin)
{
    g_landed.clear();
    FileWriter writer(fake_write, fake_cut);
    ASSERT_TRUE(writer.start());
    const std::vector<std::uint8_t> bytes(100, 1);
    ASSERT_TRUE(writer.queue(3, 0, bytes.data(), bytes.size()));
    ASSERT_TRUE(writer.queue(4, 100, bytes.data(), bytes.size()));
    writer.flush();
    writer.stop();
    ASSERT_EQ(g_landed.size(), 2u);
    EXPECT_EQ(g_landed[0].fd, 3);
    EXPECT_EQ(g_landed[1].fd, 4);
}

TEST(MusicWriter, AFailedCutIsReportedAndQueuingStops)
{
    g_landed.clear();
    FileWriter writer(fake_write, failing_cut);
    ASSERT_TRUE(writer.start());
    ASSERT_TRUE(writer.queue_cut(3, 0));
    writer.flush();
    EXPECT_NE(writer.error(), 0);
    const std::uint8_t byte = 0;
    EXPECT_FALSE(writer.queue(3, 0, &byte, 1));
    EXPECT_FALSE(writer.queue_cut(3, 0));
    writer.stop();
}

TEST(MusicWriter, StopWritesWhatIsQueued)
{
    g_landed.clear();
    FileWriter writer(fake_write, fake_cut);
    ASSERT_TRUE(writer.start());
    const std::uint8_t byte = 0;
    ASSERT_TRUE(writer.queue(3, 0, &byte, 1));
    writer.stop();
    EXPECT_EQ(g_landed.size(), 1u);
}

Drive make_drive(const char *path, bool mounted, bool music, bool songs, std::uint64_t free_bytes)
{
    Drive drive;
    drive.path = path;
    drive.mounted = mounted;
    drive.has_music = music;
    drive.has_songs = songs;
    drive.free_bytes = free_bytes;
    return drive;
}

TEST(MusicDrive, PicksTheRightDrive)
{
    constexpr std::uint64_t kRoom = 1000;
    EXPECT_EQ(pick_drive({}, kRoom), "");
    EXPECT_EQ(pick_drive({make_drive("/a", false, false, false, kRoom)}, kRoom), "")
        << "an empty mount point";
    EXPECT_EQ(pick_drive({make_drive("/a", true, false, false, kRoom)}, kRoom), "/a");
    EXPECT_EQ(pick_drive({make_drive("/a", true, false, false, kRoom),
                          make_drive("/b", true, true, false, kRoom)},
                         kRoom),
              "/b");
    EXPECT_EQ(pick_drive({make_drive("/a", true, true, false, kRoom),
                          make_drive("/b", true, true, true, kRoom)},
                         kRoom),
              "/b");
    EXPECT_EQ(pick_drive({make_drive("/a", true, true, true, kRoom - 1),
                          make_drive("/b", true, false, false, kRoom)},
                         kRoom),
              "/b")
        << "a full drive is passed over, even with the files on it";
}

TEST(MusicRing, GoesRoundBothWays)
{
    EXPECT_EQ(next_slot(0), 1);
    EXPECT_EQ(next_slot(kSlots - 1), 0);
    EXPECT_EQ(previous_slot(0), kSlots - 1);
    EXPECT_EQ(previous_slot(1), 0);
}

TEST(MusicRing, TellsWhatThePlayerDid)
{
    EXPECT_EQ(classify_move(4, 4, false, false), Move::none);
    EXPECT_EQ(classify_move(-1, 4, false, false), Move::started);
    EXPECT_EQ(classify_move(4, 5, true, false), Move::onward) << "it read to the end of the file";
    EXPECT_EQ(classify_move(4, 5, false, true), Move::onward) << "the next song was already there";
    EXPECT_EQ(classify_move(4, 5, false, false), Move::next) << "left a song part way through";
    EXPECT_EQ(classify_move(kSlots - 1, 0, false, false), Move::next) << "round the ring";
    EXPECT_EQ(classify_move(4, 3, false, false), Move::previous);
    EXPECT_EQ(classify_move(4, 3, true, false), Move::previous)
        << "the player never drifts backwards";
    EXPECT_EQ(classify_move(0, kSlots - 1, true, false), Move::previous) << "round the ring";
    EXPECT_EQ(classify_move(4, 20, false, false), Move::jumped);
}

TEST(MusicRing, AFileEndsWhereItsSongDoes)
{
    // Named 5 s into the file, with audio going in 2 s ahead of now, and the song 1 s in by now:
    // it began 2 s into the file, so the file is 2 s longer than the song.
    EXPECT_DOUBLE_EQ(named_file_seconds(5.0, 2.0, 1.0, 100.0), 102.0);
    // It began before this file did, so the file is that much shorter.
    EXPECT_DOUBLE_EQ(named_file_seconds(5.0, 2.0, 13.0, 100.0), 90.0);
    // A count that makes no sense never ends the file before what is written.
    EXPECT_DOUBLE_EQ(named_file_seconds(50.0, 2.0, 200.0, 100.0), 50.0 + kShortestSeconds);
}

std::uint64_t byte_at(double seconds)
{
    return frame_offset(static_cast<std::uint32_t>(seconds * kRate / kBlock));
}

TEST(MusicPlayHead, FollowsTheReadPositionOnceItHasFilledItsReadAhead)
{
    PlayHead head;
    head.opened(100.0, kAudioStart);
    head.update(100.2, byte_at(10.0), byte_at(300.0), 300.0);
    EXPECT_NEAR(head.heard(), 0.3, 0.3);
    head.update(120.0, byte_at(30.0), byte_at(300.0), 300.0);
    EXPECT_NEAR(head.heard(), 30.0 - kReadAheadSeconds, 0.1);
    EXPECT_FALSE(head.paused());
}

TEST(MusicPlayHead, AStopShortOfTheEndIsAPause)
{
    PlayHead head;
    head.opened(100.0, byte_at(10.0));
    head.update(110.0, byte_at(20.0), byte_at(300.0), 300.0);
    head.update(112.0, byte_at(20.0), byte_at(300.0), 300.0);
    EXPECT_FALSE(head.paused()) << "a slow drive stops it for a moment";
    head.update(113.5, byte_at(20.0), byte_at(300.0), 300.0);
    EXPECT_TRUE(head.paused());
    EXPECT_NEAR(head.heard(), 20.0 - kReadAheadSeconds, 0.1)
        << "the time it was stopped does not count";
    head.update(130.0, byte_at(20.0), byte_at(300.0), 300.0);
    EXPECT_NEAR(head.heard(), 20.0 - kReadAheadSeconds, 0.1);
    head.update(131.0, byte_at(21.0), byte_at(300.0), 300.0);
    EXPECT_FALSE(head.paused());
}

TEST(MusicPlayHead, ReadingToTheEndIsNotAPauseAndCountsOn)
{
    PlayHead head;
    const auto end = byte_at(60.0);
    head.opened(100.0, byte_at(10.0));
    head.update(150.0, byte_at(60.0), end, 60.0);
    const double when_read = head.heard();
    head.update(155.0, byte_at(60.0), end, 60.0);
    EXPECT_FALSE(head.paused());
    EXPECT_NEAR(head.heard(), when_read + 5.0, 0.01);
}

TEST(MusicPlayHead, LingeringOnAShortFileAfterAPressIsNotAPause)
{
    PlayHead head;
    const auto end = byte_at(4.0);
    head.opened(100.0, end);
    for (double now = 100.5; now < 108.0; now += 0.5)
    {
        head.update(now, end, end, 4.0);
        EXPECT_FALSE(head.paused()) << "at " << now;
    }
}

TEST(MusicPlayHead, LongPastTheEndOfAFileIsAPause)
{
    PlayHead head;
    const auto end = byte_at(4.0);
    head.opened(100.0, end);
    head.update(120.0, end, end, 4.0);
    EXPECT_TRUE(head.paused());
}

TEST(MusicPlayHead, StillFillingItsReadAheadIsNotAPause)
{
    PlayHead head;
    head.opened(100.0, kAudioStart);
    head.update(100.1, byte_at(1.0), byte_at(300.0), 300.0);
    head.update(106.0, byte_at(1.0), byte_at(300.0), 300.0);
    EXPECT_FALSE(head.paused());
}

TEST(MusicWire, WritesTheServerAfterTheMarker)
{
    using namespace hui::afterglow::music_wire;
    std::string elf = std::string("before") + kServerMarker + std::string(200, '\0') + "after";
    const auto size = elf.size();
    ASSERT_TRUE(embed_server(elf, "192.0.2.4", 8927));
    EXPECT_EQ(elf.size(), size) << "nothing in the file may move";
    const auto at = elf.find(kServerMarker) + sizeof kServerMarker - 1;
    EXPECT_STREQ(elf.c_str() + at, "192.0.2.4:8927");
    ASSERT_TRUE(embed_server(elf, "192.0.2.5", 1));
    EXPECT_STREQ(elf.c_str() + at, "192.0.2.5:1") << "a shorter address leaves nothing behind";
    EXPECT_EQ(elf.substr(elf.size() - 5), "after");
}

TEST(MusicWire, RefusesWhatDoesNotFit)
{
    using namespace hui::afterglow::music_wire;
    std::string none(300, 'x');
    EXPECT_FALSE(embed_server(none, "192.0.2.4", 8927)) << "no marker";
    std::string room = std::string(kServerMarker) + std::string(200, '\0');
    EXPECT_FALSE(embed_server(room, std::string(100, 'h'), 8927)) << "too long";
    std::string tail = std::string(kServerMarker) + "x";
    EXPECT_FALSE(embed_server(tail, "192.0.2.4", 8927)) << "the end of the file is too near";
}

TEST(MusicWire, ReadsThePayloadsStatusLine)
{
    using namespace hui::afterglow::music_wire;
    EXPECT_EQ(parse_state("ready"), PayloadState::ready);
    EXPECT_EQ(parse_state("no-drive"), PayloadState::no_drive);
    EXPECT_EQ(parse_state("waiting"), PayloadState::waiting);
    EXPECT_EQ(parse_state("playing King Kunta - Kendrick Lamar"), PayloadState::playing);
    EXPECT_EQ(song_in("playing King Kunta - Kendrick Lamar"), "King Kunta - Kendrick Lamar");
    EXPECT_EQ(song_in("ready"), "");
    EXPECT_EQ(parse_state(""), PayloadState::unknown);
    EXPECT_EQ(parse_state("nonsense here"), PayloadState::unknown);
}

TEST(MusicToast, StampsAreUtc)
{
    EXPECT_EQ(utc_stamp(7), "1970-01-01T00:00:00.007Z");
    EXPECT_EQ(utc_stamp(951782400007), "2000-02-29T00:00:00.007Z");
    EXPECT_EQ(utc_stamp(1798761599007), "2026-12-31T23:59:59.007Z");
}

TEST(MusicToast, EscapesWhatJsonCannotHold)
{
    EXPECT_EQ(json_escaped("Say \"Hi\"\\ now"), "Say \\\"Hi\\\"\\\\ now");
    EXPECT_EQ(json_escaped(std::string("a\nb\x01")), "a\\u000ab\\u0001");
    EXPECT_EQ(json_escaped("Bj\xc3\xb6rk"), "Bj\xc3\xb6rk");
}

TEST(MusicToast, CarriesTheSong)
{
    const auto toast = track_toast("Song", "Singer", "/user/x.png", 1000);
    EXPECT_NE(toast.find("\"message\":{\"body\":\"Song\"}"), std::string::npos);
    EXPECT_NE(toast.find("\"subMessage\":{\"body\":\"Singer\"}"), std::string::npos);
    EXPECT_NE(toast.find("\"url\":\"/user/x.png\""), std::string::npos);
    EXPECT_NE(toast.find("InteractiveToastTemplateB"), std::string::npos);
}
} // namespace
