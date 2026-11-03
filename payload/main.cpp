// Afterglow - Music over games: a Music Assistant speaker heard through the USB music player.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A payload's own audio port is silent, but the console's USB music player keeps playing over
// games. So this joins Music Assistant with the real Afterglow Client and writes the sound into
// FLAC files just ahead of where the player is reading. Writing audio ahead of the player comes
// from librespot-ps5 v0.1.0 (GPL-3.0-or-later, stupiditytries).
//
// Each song has a file of its own, so the Music card shows its title, artist and length. The
// player is started by hand on any file in "Afterglow Live"; from then on the card's buttons work
// Music Assistant. See ring_plan.hpp and player_watch.hpp. The app stops it through ControlPort, or
// a file named "stop" in the work folder does; the player stays on the waiting files until it is
// back.
#include "afterglow/client.hpp"
#include "control_port.hpp"
#include "drive_finder.hpp"
#include "file_writer.hpp"
#include "flac_writer.hpp"
#include "now_playing.hpp"
#include "player_watch.hpp"
#include "ring_plan.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

extern "C" int sceNotificationSend(int user, bool logged, const char *payload);

// The app writes the server's address into its copy of this file, after the marker.
extern "C"
    __attribute__((used)) char afterglow_music_server[hui::afterglow::music_wire::kServerRoom] =
        "AFTERGLOW-MUSIC-SERVER=";

namespace
{
constexpr const char *kWorkDir = "/data/afterglow-music";
constexpr int kSystemUser = 0xFE;
constexpr long kLogBytes = 8 * 1024 * 1024;

std::mutex g_log_mutex;
FILE *g_log = nullptr;

std::int64_t now_us()
{
    timespec value{};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return static_cast<std::int64_t>(value.tv_sec) * 1000000 + value.tv_nsec / 1000;
}

void say(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log)
    {
        std::fprintf(g_log, "%9.2f ", static_cast<double>(now_us()) / 1e6);
        std::vfprintf(g_log, format, args);
        std::fputc('\n', g_log);
        std::fflush(g_log);
    }
    va_end(args);
}

// A payload left running from boot must not fill the console's drive with its log.
void rotate_log()
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log || std::ftell(g_log) < kLogBytes)
        return;
    const std::string path = std::string(kWorkDir) + "/music.log";
    std::fclose(g_log);
    std::rename(path.c_str(), (path + ".old").c_str());
    g_log = std::fopen(path.c_str(), "a");
}
} // namespace

namespace hui::sys
{
std::int64_t monotonic_us()
{
    return now_us();
}
void log(const char *format, ...)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log)
        return;
    va_list args;
    va_start(args, format);
    std::vfprintf(g_log, format, args);
    va_end(args);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}
} // namespace hui::sys

namespace
{
using namespace afterglow::usbflac;

struct Config
{
    std::string host, drive = "auto", client_id = "afterglow-ps5-music",
                      name = "Afterglow over games", icon = "/user/appmeta/PPSA99481/icon0.png";
    int port = 8927;
    double lead = 16.0, status_every = 30.0;
    bool toasts = true, card_controls = true;
};

// The server the app asked for, if it was the app that started this copy.
void read_embedded_server(Config &config)
{
    constexpr auto marker = sizeof hui::afterglow::music_wire::kServerMarker - 1;
    const std::string text(afterglow_music_server + marker);
    const auto colon = text.rfind(':');
    if (colon == std::string::npos)
        return;
    config.host = text.substr(0, colon);
    config.port = std::atoi(text.c_str() + colon + 1);
}

// Everything has a default, and the app supplies the server, so the file is for tuning by hand.
bool read_config(Config &config)
{
    const std::string path = std::string(kWorkDir) + "/music.conf";
    FILE *file = std::fopen(path.c_str(), "r");
    if (!file)
    {
        read_embedded_server(config);
        return !config.host.empty();
    }
    char line[256];
    while (std::fgets(line, sizeof line, file))
    {
        line[std::strcspn(line, "\r\n")] = 0;
        char *equals = std::strchr(line, '=');
        if (!equals)
            continue;
        *equals = 0;
        const std::string key = line, value = equals + 1;
        if (key == "host")
            config.host = value;
        else if (key == "port")
            config.port = std::atoi(value.c_str());
        else if (key == "drive")
            config.drive = value;
        else if (key == "lead")
            config.lead = std::atof(value.c_str());
        else if (key == "status")
            config.status_every = std::atof(value.c_str());
        else if (key == "icon")
            config.icon = value;
        else if (key == "toasts")
            config.toasts = value != "0";
        else if (key == "card")
            config.card_controls = value != "0";
    }
    std::fclose(file);
    read_embedded_server(config);
    const auto within = [](double value, double low, double high)
    { return std::isfinite(value) && value >= low && value <= high; };
    // The lead must stay above how far ahead the player reads.
    if (!within(config.lead, kReadAheadSeconds + 2.0, 30.0))
        config.lead = 16.0;
    if (!within(config.status_every, 1.0, 600.0))
        config.status_every = 30.0;
    if (config.port <= 0 || config.port > 65535)
        config.port = 8927;
    return !config.host.empty();
}

constexpr const char *kFolder = "Afterglow Live";

std::uint32_t frames_in(double seconds)
{
    const double frames = seconds * kRate / kBlock;
    const auto whole = static_cast<std::uint32_t>(frames);
    return static_cast<double>(whole) < frames ? whole + 1 : whole;
}

double seconds_in(std::uint32_t frames)
{
    return static_cast<double>(frames) * kBlock / kRate;
}

bool is_folder(const std::string &path)
{
    struct stat info
    {
    };
    return !stat(path.c_str(), &info) && S_ISDIR(info.st_mode);
}

// A USB mount point is there with or without a drive; it is a drive only if it is on another
// device than the folder that holds it.
bool is_mounted(const std::string &path)
{
    struct stat here
    {
    }, above{};
    return !stat(path.c_str(), &here) && !stat("/mnt", &above) && here.st_dev != above.st_dev;
}

Drive look_at_drive(const std::string &path)
{
    Drive drive;
    drive.path = path;
    drive.mounted = is_mounted(path);
    if (!drive.mounted)
        return drive;
    drive.has_music = is_folder(path + "/Music");
    drive.has_songs = is_folder(path + "/Music/" + kFolder);
    struct statfs space
    {
    };
    if (!statfs(path.c_str(), &space))
        drive.free_bytes = static_cast<std::uint64_t>(space.f_bavail) * space.f_bsize;
    return drive;
}

std::string find_drive(const std::string &wanted, std::uint64_t room_needed)
{
    std::vector<Drive> drives;
    if (wanted != "auto")
        drives.push_back(look_at_drive(wanted));
    else
        for (int number = 0; number < 8; ++number)
            drives.push_back(look_at_drive("/mnt/usb" + std::to_string(number)));
    return pick_drive(drives, room_needed);
}

std::int64_t wall_clock_ms()
{
    timeval value{};
    gettimeofday(&value, nullptr);
    return static_cast<std::int64_t>(value.tv_sec) * 1000 + value.tv_usec / 1000;
}

void show_toast(const Config &config, const std::string &title, const std::string &detail)
{
    if (!config.toasts)
        return;
    const auto toast = track_toast(title, detail, config.icon, wall_clock_ms());
    const int result = sceNotificationSend(kSystemUser, true, toast.c_str());
    say("toast: %s - %s (0x%x)", title.c_str(), detail.c_str(), static_cast<unsigned>(result));
}

// Every file is some seconds of silence until a song is written over it. The player reads a file
// only as far as the size it had when opened, then goes on to the next, and it stops altogether if
// the file ends short of that. So a song's file is made the song's length before the player gets to
// it, and is never cut after. Audio runs on from one file into the next at that length. Music
// Assistant names the new song a few seconds later, while the player still has time to find it: it
// reads the next file's tags when it has read to the end of this one.
class Stream
{
  public:
    Stream(const Config &config, hui::afterglow::Client &client) : config_(config), client_(client)
    {
        const Tags tags{kFolder, "Waiting for music", ""};
        waiting_ = header(std::uint64_t{frames_in(kWaitingSeconds)} * kBlock, tags);
        for (std::uint32_t index = 0; index < frames_in(kWaitingSeconds); ++index)
            push_silence(waiting_, index);
    }
    ~Stream()
    {
        close_files();
    }
    void tick(double now)
    {
        if (!open_)
        {
            if (now >= next_open_)
            {
                next_open_ = now + kDriveRetry;
                open_files();
            }
            drain();
            return;
        }
        if (writer_.error())
        {
            say("write failed errno=%d: the drive may be gone", writer_.error());
            close_files();
            next_open_ = now + kDriveRetry;
            drain();
            return;
        }
        rendered_ = false;
        follow_player(now);
        follow_music(now);
        write_audio(now);
        keep_silence_ahead();
        if (!rendered_)
            drain();
    }
    // What has been written stays, and the player runs on to the waiting files.
    void finish()
    {
        writing_ = false;
        writer_.flush();
    }
    // A line for the app: what is going on, and the song if there is one.
    std::string summary() const
    {
        using namespace hui::afterglow::music_wire;
        if (!open_)
            return state_word(PayloadState::no_drive);
        if (play_ < 0)
            return state_word(PayloadState::ready);
        if (!slots_[play_].song)
            return state_word(PayloadState::waiting);
        const auto &tags = slots_[play_].tags;
        auto line = std::string(state_word(PayloadState::playing)) + " " + tags.title;
        if (!tags.artist.empty())
            line += " - " + tags.artist;
        return line;
    }
    void note_gap(double gap)
    {
        max_gap_ = gap > max_gap_ ? gap : max_gap_;
    }
    void status(double now)
    {
        const auto shot = client_.snapshot();
        const double spare =
            writing_ && play_ == write_ ? seconds_in(write_frames_) - playing_read_seconds_ : -1.0;
        min_spare_ = spare >= 0 && spare < min_spare_ ? spare : min_spare_;
        say("play=%d write=%d%s heard=%.1f read=%.1f written=%.1f ahead=%.2f spare_min=%.2f "
            "paused=%d jumps=%d gap=%.2f wr=%.2f ma=%d "
            "sync=%d playing=%d buf=%.0fms underruns=%llu overflow=%llu holes=%llu hole_ms=%llu "
            "queued=%.2fs writes=%llu "
            "track_ms=%lld status=%s",
            play_, write_, writing_ ? "" : " (none)", head_.heard(), head_.seconds_read(),
            seconds_in(write_frames_), ahead_seconds(), min_spare_ > 1e8 ? -1.0 : min_spare_,
            paused_, jumps_, max_gap_, writer_.take_longest_write(), shot.connected,
            shot.clock_synced, shot.playing, shot.buffered_ms,
            static_cast<unsigned long long>(shot.underruns),
            static_cast<unsigned long long>(shot.overflows),
            static_cast<unsigned long long>(holes_),
            static_cast<unsigned long long>(hole_frames_ * 1000 / kRate),
            static_cast<double>(writer_.queued_bytes()) / kWriteBytesPerSecond,
            static_cast<unsigned long long>(writer_.writes()),
            static_cast<long long>(shot.track_ms), shot.status.c_str());
        (void)now;
        max_gap_ = 0;
        min_spare_ = 1e9;
    }
    bool can_watch(const std::string &readable_path)
    {
        return watch_.self_check(readable_path.c_str());
    }

  private:
    struct Slot
    {
        int fd = -1;
        bool song = false;        // Holds a song, not a moment of silence.
        std::uint32_t frames = 0; // Its length, fixed before the player opens it.
        Tags tags;
    };
    // Longer than the player reads ahead, by long enough to get a new song's tags in place after
    // a press on the card, before the player reads the end of this file.
    static constexpr double kWaitingSeconds = 14.0;
    static constexpr double kSilenceAhead = 30.0;
    static constexpr double kSilenceStep = 6.0;
    static constexpr double kSlack = 1.0;
    static constexpr double kDriveRetry = 5.0;
    static constexpr double kLookEvery = 0.2;
    // A file change takes up to 0.6 s with no file open.
    static constexpr double kGoneSeconds = 2.0;
    // The server's answer to a skip.
    static constexpr double kNewTrackWait = 6.0;
    static constexpr double kNoTitleWait = 2.0;
    // A read position this far back on a waiting file means it was started over.
    static constexpr std::uint64_t kStartedOverBytes = 1024 * 1024;
    // A song without a stated length gets a file this long.
    static constexpr double kUnknownLength = 3600.0;
    // Audio past the end of a file with no new song named yet goes in a file of this length, which
    // becomes the new song's if it is named before the player opens it.
    static constexpr double kRunOnSeconds = 60.0;
    static constexpr std::uint64_t kRoomNeeded = 256 * 1024 * 1024;
    // Audio and the silence ahead of it each take two bytes a sample on two channels.
    static constexpr double kWriteBytesPerSecond = 2.0 * 2.0 * 2.0 * kRate;
    // A drive this far behind gets audio only until it recovers.
    static constexpr std::size_t kMostQueuedForSilence = 3 * 1024 * 1024;
    // How far ahead of now audio has arrived: the server sends about 4 s ahead.
    static constexpr std::int64_t kRenderAheadUs = 2000000;
    // A cursor this far behind now has missed its audio and jumps to now. Less than this,
    // it catches up from the client's buffer, which holds 9 s.
    static constexpr std::int64_t kRenderBehindUs = 1500000;
    static constexpr int kFramesPerTick = 400;
    static constexpr std::int64_t kBlockUs = static_cast<std::int64_t>(kBlock) * 1000000 / kRate;

    std::string slot_path(const std::string &folder, int slot) const
    {
        char name[32];
        std::snprintf(name, sizeof name, "/Afterglow %02d.flac", slot + 1);
        return folder + name;
    }
    bool open_files()
    {
        const auto drive = find_drive(config_.drive, kRoomNeeded);
        if (drive.empty())
        {
            if (!told_no_drive_)
            {
                say("no USB drive with room for the song files: waiting for one");
                show_toast(config_, "Music over games", "Plug in a USB drive to play over games");
            }
            told_no_drive_ = true;
            return false;
        }
        told_no_drive_ = false;
        const std::string music = drive + "/Music", folder = music + "/" + kFolder;
        mkdir(music.c_str(), 0777);
        mkdir(folder.c_str(), 0777);
        if (!writer_.start())
        {
            say("cannot start the writer thread");
            return false;
        }
        watch_.clear_slots();
        for (int slot = 0; slot < kSlots; ++slot)
        {
            const auto path = slot_path(folder, slot);
            slots_[slot] = Slot{};
            slots_[slot].fd = open(path.c_str(), O_RDWR | O_CREAT, 0666);
            if (slots_[slot].fd < 0)
            {
                say("cannot open %s errno=%d", path.c_str(), errno);
                close_files();
                return false;
            }
            watch_.add_slot(slot, slots_[slot].fd);
        }
        // The player may have stayed on these files while this payload was away.
        const auto seen = watch_.look();
        for (int slot = 0; slot < kSlots; ++slot)
        {
            struct stat info
            {
            };
            const bool as_left = !fstat(slots_[slot].fd, &info) &&
                                 static_cast<std::size_t>(info.st_size) == waiting_.size();
            slots_[slot].frames = frames_in(kWaitingSeconds);
            if (slot == seen.slot && !as_left)
            {
                const auto bytes = static_cast<std::uint64_t>(info.st_size);
                slots_[slot].frames =
                    bytes > kAudioStart
                        ? static_cast<std::uint32_t>((bytes - kAudioStart) /
                                                     (frame_offset(1000) - frame_offset(999)))
                        : 0;
            }
            else if (!as_left)
                reset_slot(slot);
        }
        writer_.flush();
        play_ = -1;
        writing_ = false;
        write_frames_ = silent_to_ = capacity_ = 0;
        render_us_ = 0;
        paused_ = paused_music_ = false;
        open_ = true;
        say("song files ready in %s", folder.c_str());
        if (seen.slot < 0)
            show_toast(config_, "Music over games is ready",
                       std::string("Play ") + kFolder + " in Music");
        return true;
    }
    void close_files()
    {
        writer_.stop();
        writer_.clear_error();
        watch_.clear_slots();
        for (auto &slot : slots_)
        {
            if (slot.fd >= 0)
                close(slot.fd);
            slot = Slot{};
        }
        open_ = writing_ = false;
        play_ = -1;
    }
    void reset_slot(int slot)
    {
        auto &file = slots_[slot];
        writer_.queue_cut(file.fd, 0);
        writer_.queue(file.fd, 0, waiting_.data(), waiting_.size());
        file.song = false;
        file.frames = frames_in(kWaitingSeconds);
        file.tags = Tags{};
    }
    // Follows the player, which has moved on when the file it has open changes.
    void follow_player(double now)
    {
        if (now < next_look_)
            return;
        next_look_ = now + kLookEvery;
        const auto seen = watch_.look();
        if (seen.slot < 0)
        {
            if (play_ < 0)
                return;
            if (gone_since_ < 0)
                gone_since_ = now;
            if (now - gone_since_ >= kGoneSeconds)
                player_left();
            return;
        }
        gone_since_ = -1;
        if (seen.slot != play_)
        {
            player_moved(now, seen.slot, seen.read);
            last_read_ = seen.read;
            return;
        }
        // A waiting file started over, after the player stopped at the end of its list: what has
        // been written since is not joined up with what comes next.
        if (!slots_[play_].song && seen.read + kStartedOverBytes < last_read_)
        {
            say("file %d started over", play_);
            abandon_songs(-1, play_);
            head_.opened(now, seen.read);
        }
        last_read_ = seen.read;
        const auto &slot = slots_[play_];
        head_.update(now, seen.read, frame_offset(slot.frames), seconds_in(slot.frames));
        playing_read_seconds_ = head_.seconds_read();
        if (play_ == write_ && writing_)
        {
            const double spare = seconds_in(write_frames_) - playing_read_seconds_;
            min_spare_ = spare < min_spare_ ? spare : min_spare_;
        }
        if (head_.paused() != paused_)
        {
            paused_ = head_.paused();
            say("%s at the console, %.1f s into file %d", paused_ ? "paused" : "resumed",
                head_.heard(), play_);
            if (paused_ && config_.card_controls && client_.snapshot().playing)
            {
                paused_music_ = true;
                client_.command(hui::afterglow::Command::pause);
                say("told Music Assistant to pause");
            }
            else if (!paused_ && paused_music_)
            {
                paused_music_ = false;
                render_us_ = 0;
                client_.command(hui::afterglow::Command::play);
                say("told Music Assistant to play");
            }
        }
    }
    void player_moved(double now, int to, std::uint64_t read)
    {
        const int from = play_;
        // Once it has read to the end of a file it is on its way to the next, press or no press.
        const bool read_all = from >= 0 && last_read_ >= frame_offset(slots_[from].frames);
        const auto move = classify_move(from, to, read_all, slots_[to].song);
        say("player on file %d (was %d, %s), read %.1f s", to, from, move_name(move),
            read > kAudioStart ? static_cast<double>(read - kAudioStart) / 192000.0 : 0.0);
        play_ = to;
        head_.opened(now, read);
        paused_ = false;
        // It got there before the audio did, on a drive that fell behind.
        if (move == Move::onward && writing_ && from == write_)
            writing_ = false;
        if (from >= 0 && !(writing_ && from == write_))
            reset_slot(from);
        switch (move)
        {
        case Move::next:
        case Move::previous:
        case Move::jumped:
            abandon_songs(from, to);
            if (config_.card_controls && move != Move::jumped)
            {
                client_.command(move == Move::next ? hui::afterglow::Command::next
                                                   : hui::afterglow::Command::previous);
                say("told Music Assistant to go to the %s track",
                    move == Move::next ? "next" : "previous");
                waiting_title_ = client_.snapshot().title;
                waiting_track_until_ = now + kNewTrackWait;
            }
            break;
        case Move::started:
        case Move::onward:
            if (paused_music_)
            {
                paused_music_ = false;
                render_us_ = 0;
                client_.command(hui::afterglow::Command::play);
                say("told Music Assistant to play");
            }
            break;
        case Move::none:
            break;
        }
        if (slots_[to].song)
            show_toast(config_, slots_[to].tags.title, slots_[to].tags.artist);
    }
    // What was being written is not what comes next any more.
    void abandon_songs(int from, int to)
    {
        writing_ = false;
        render_us_ = 0;
        paused_music_ = false;
        for (int slot = 0; slot < kSlots; ++slot)
            if (slot != to && (slots_[slot].song || slot == from))
                reset_slot(slot);
    }
    // Stopped, or on music of the owner's own: nothing here is to be heard until it comes back.
    void player_left()
    {
        say("the player has left these files");
        const int from = play_;
        abandon_songs(from, -1);
        play_ = -1;
        gone_since_ = -1;
        paused_ = false;
        if (config_.card_controls && client_.snapshot().playing)
        {
            paused_music_ = true;
            client_.command(hui::afterglow::Command::pause);
            say("told Music Assistant to pause");
        }
    }
    static const char *move_name(Move move)
    {
        switch (move)
        {
        case Move::none:
            return "same";
        case Move::started:
            return "started";
        case Move::onward:
            return "onward";
        case Move::next:
            return "next";
        case Move::previous:
            return "previous";
        case Move::jumped:
            return "jumped";
        }
        return "?";
    }
    void follow_music(double now)
    {
        const auto shot = client_.snapshot();
        const bool flowing = shot.playing && client_.output_state().active;
        flowing_since_ = flowing ? (flowing_since_ < 0 ? now : flowing_since_) : -1;
        note_progress(now, shot);
        if (writing_)
        {
            const bool new_song =
                !shot.title.empty() && (shot.title != named_.title || shot.artist != named_.artist);
            if (new_song)
                song_named(shot);
            else if (write_frames_ >= capacity_)
            {
                // Named too late for the file it started in, or not named yet.
                const bool named = named_.title != slots_[write_].tags.title;
                say("file %d is full", write_);
                begin_file(named_, named ? seconds_left(now, shot) : kRunOnSeconds);
            }
            return;
        }
        if (!flowing || play_ < 0 || paused_)
            return;
        if (now < waiting_track_until_ && shot.title == waiting_title_)
            return;
        if (shot.title.empty() && now - flowing_since_ < kNoTitleWait)
            return;
        named_ = tags_of(shot);
        begin_file(named_, seconds_left(now, shot));
    }
    // Music Assistant has named a song other than the one last named. It does that a few seconds
    // into the song, by which time its audio is already going into a file.
    void song_named(const hui::afterglow::Snapshot &shot)
    {
        named_ = tags_of(shot);
        say("new song, %lld ms in by the server's count, %.1f s into file %d: %s",
            static_cast<long long>(shot.elapsed_ms), seconds_in(write_frames_), write_,
            named_.title.c_str());
        log_recent_loudness();
        if (write_ != play_)
        {
            // The player has not opened that file, so it can still become this song's.
            const double ahead_of_now =
                render_us_ ? static_cast<double>(render_us_ - now_us()) / 1e6 : 0.0;
            const double length =
                shot.track_ms > 0
                    ? named_file_seconds(seconds_in(write_frames_), ahead_of_now,
                                         static_cast<double>(shot.elapsed_ms) / 1000.0,
                                         static_cast<double>(shot.track_ms) / 1000.0)
                    : kUnknownLength;
            size_file(write_, named_, length);
            return;
        }
        // Too late for this file: a skip from elsewhere, or a song that ended early. Its audio
        // runs on in this file, and the next file is named for it.
        show_toast(config_, named_.title, named_.artist);
    }
    Tags tags_of(const hui::afterglow::Snapshot &shot) const
    {
        return Tags{shot.title.empty() ? kFolder : shot.title, shot.artist, shot.album};
    }
    void note_progress(double now, const hui::afterglow::Snapshot &shot)
    {
        if (shot.elapsed_ms == progress_elapsed_ms_ && shot.track_ms == progress_track_ms_)
            return;
        progress_elapsed_ms_ = shot.elapsed_ms;
        progress_track_ms_ = shot.track_ms;
        progress_seen_at_ = now;
    }
    // How much of the song is left, going by the server's last word on how far in it is.
    double seconds_left(double now, const hui::afterglow::Snapshot &shot) const
    {
        if (shot.track_ms <= 0)
            return kUnknownLength;
        const double left = static_cast<double>(shot.track_ms - shot.elapsed_ms) / 1000.0 -
                            (now - progress_seen_at_);
        return left > kShortestSeconds ? left : kShortestSeconds;
    }
    // The next file to fill. The player reads a file's tags when it has read to the end of the one
    // before, so once it has done that a song has to go in the file after.
    int next_free_slot() const
    {
        if (slots_[write_].song)
            return next_slot(write_);
        if (play_ < 0)
            return 0;
        const bool tags_read = last_read_ >= frame_offset(slots_[play_].frames);
        return tags_read ? next_slot(next_slot(play_)) : next_slot(play_);
    }
    void begin_file(const Tags &tags, double seconds)
    {
        const int slot = next_free_slot();
        if (slot == play_)
        {
            say("the ring is full: this song waits for the player");
            writing_ = false;
            return;
        }
        size_file(slot, tags, seconds);
        write_ = slot;
        writing_ = true;
        write_frames_ = silent_to_ = 0;
    }
    // Empties a file the player has not opened and gives it these tags and this length.
    void size_file(int slot_number, const Tags &tags, double seconds)
    {
        auto &file = slots_[slot_number];
        const auto start = header(static_cast<std::uint64_t>(seconds * kRate), tags);
        const auto frames = frames_in(seconds);
        const bool empty_it = !file.song;
        if (empty_it)
            writer_.queue_cut(file.fd, 0);
        writer_.queue_cut(file.fd, frame_offset(frames));
        writer_.queue(file.fd, 0, start.data(), start.size());
        file.song = true;
        file.frames = frames;
        file.tags = tags;
        capacity_ = frames;
        say("file %d is now: %s - %s (%.0f s)", slot_number, tags.title.c_str(),
            tags.artist.c_str(), seconds);
    }
    // How much is in the files between what is heard and the write head.
    double ahead_seconds() const
    {
        if (play_ < 0 || !writing_)
            return 0.0;
        const double written = seconds_in(write_frames_);
        if (play_ == write_)
            return written - head_.heard();
        const double left = seconds_in(slots_[play_].frames) - head_.heard();
        double total = left > 0 ? left : 0.0;
        for (int slot = next_slot(play_); slot != write_; slot = next_slot(slot))
            total += seconds_in(slots_[slot].frames);
        return total + written;
    }
    void push_silence(std::vector<std::uint8_t> &out, std::uint32_t index)
    {
        silence_.assign(kBlock * 2, 0);
        push_frame(out, index, silence_.data());
    }
    bool write_frame(const std::vector<std::uint8_t> &bytes)
    {
        const auto done = writer_.queue(slots_[write_].fd, frame_offset(write_frames_),
                                        bytes.data(), bytes.size());
        if (done)
            ++write_frames_;
        return done;
    }
    void keep_silence_ahead()
    {
        if (writing_ && writer_.queued_bytes() < kMostQueuedForSilence)
            top_up_silence();
    }
    // Silence, not the end of the file, after the write head: if the player ever reads past the
    // audio it hears a moment of silence, where the end of the file would make it move on.
    void top_up_silence()
    {
        if (silent_to_ < write_frames_)
            silent_to_ = write_frames_;
        const auto to = next_silence_end(silent_to_, write_frames_, frames_in(kSilenceAhead),
                                         frames_in(kSilenceStep), capacity_);
        if (to == silent_to_)
            return;
        std::vector<std::uint8_t> frames;
        for (auto index = silent_to_; index < to; ++index)
            push_silence(frames, index);
        if (writer_.queue(slots_[write_].fd, frame_offset(silent_to_), frames.data(),
                          frames.size()))
            silent_to_ = to;
    }
    void write_audio(double now)
    {
        if (!writing_ || play_ < 0)
            return;
        const auto shot_flowing = client_.output_state().active;
        // With nothing coming the player hears the blank part of the file, which plays as silence.
        if (!shot_flowing)
            return;
        // Audio written as it arrives counts towards the lead, so less silence is needed first.
        const double wanted = config_.lead - static_cast<double>(kRenderAheadUs) / 1e6 - kSlack;
        for (int i = 0; i < kFramesPerTick && write_frames_ < capacity_; ++i)
        {
            if (writer_.backlogged())
                break;
            if (!paused_ && ahead_seconds() < wanted)
            {
                // Too near the player to fill with audio, which only arrives a little ahead of
                // when it is due: silence first, then audio from now on.
                frame_.clear();
                push_silence(frame_, write_frames_);
                if (!write_frame(frame_))
                    return;
                continue;
            }
            const auto clock = now_us();
            if (render_us_ < clock - kRenderBehindUs)
            {
                if (render_us_)
                {
                    ++jumps_;
                    say("audio cursor %.2f s behind: it jumps to now",
                        static_cast<double>(clock - render_us_) / 1e6);
                }
                render_us_ = clock;
            }
            // The audio for the next frame has not arrived yet.
            if (render_us_ + kBlockUs > clock + kRenderAheadUs)
                break;
            if (!render_frame())
                return;
        }
        (void)now;
    }
    bool render_frame()
    {
        for (std::uint32_t offset = 0; offset < kBlock; offset += 1024)
            client_.render_at(block_.data() + std::size_t{offset} * 2, 1024,
                              render_us_ + static_cast<std::int64_t>(offset) * 1000000 / kRate);
        rendered_ = true;
        render_us_ += kBlockUs;
        note_loudness();
        frame_.clear();
        push_frame(frame_, write_frames_, block_.data());
        return write_frame(frame_);
    }
    // Stretches of at least 5 ms with nothing in them: dropped audio, or real silence in the
    // music, and the overflow count tells which. The loudness around a song's end shows how well
    // the server's word of a new song lines up with the audio.
    void note_loudness()
    {
        double sum = 0;
        for (const auto sample : block_)
            sum += static_cast<double>(sample) * sample;
        loudness_[loudness_at_++ % kLoudnessFrames] =
            static_cast<std::uint16_t>(std::sqrt(sum / static_cast<double>(block_.size())));
        for (std::uint32_t pair = 0; pair < kBlock; ++pair)
        {
            if (block_[pair * 2] || block_[pair * 2 + 1])
            {
                zero_run_ = 0;
                continue;
            }
            ++zero_run_;
            if (zero_run_ == 240)
            {
                hole_frames_ += 240;
                ++holes_;
            }
            else if (zero_run_ > 240)
                ++hole_frames_;
        }
    }
    void log_recent_loudness()
    {
        char text[kLoudnessFrames * 7 + 1];
        std::size_t used = 0;
        constexpr std::uint32_t kEveryFrames = 6;
        for (std::uint32_t back = kLoudnessFrames; back >= kEveryFrames; back -= kEveryFrames)
        {
            int largest = 0;
            for (std::uint32_t i = 0; i < kEveryFrames; ++i)
                largest = std::max<int>(
                    largest,
                    loudness_[(loudness_at_ + kLoudnessFrames * 2 - back + i) % kLoudnessFrames]);
            used += static_cast<std::size_t>(
                std::snprintf(text + used, sizeof text - used, "%d ", largest));
        }
        say("loudness, each 0.5 s up to the write head: %s", text);
    }
    // The client's network side waits for the audio side after a clear, so the queue is
    // read every tick even when nothing is written. Only audio already due is dropped.
    void drain()
    {
        client_.render_at(scratch_.data(), 1024, now_us() - kRenderBehindUs - 500000);
    }

    static constexpr std::uint32_t kLoudnessFrames = 72;

    const Config &config_;
    hui::afterglow::Client &client_;
    FileWriter writer_;
    PlayerWatch watch_;
    PlayHead head_;
    Slot slots_[kSlots];
    std::vector<std::uint8_t> waiting_;
    bool open_ = false, told_no_drive_ = false, writing_ = false, rendered_ = false;
    bool paused_ = false, paused_music_ = false;
    int play_ = -1, write_ = 0, jumps_ = 0;
    std::uint32_t capacity_ = 0, write_frames_ = 0, silent_to_ = 0, zero_run_ = 0;
    std::uint32_t loudness_at_ = 0;
    std::uint16_t loudness_[kLoudnessFrames] = {};
    double next_open_ = 0, next_look_ = 0, gone_since_ = -1, flowing_since_ = -1;
    double waiting_track_until_ = 0, playing_read_seconds_ = 0, progress_seen_at_ = 0;
    double max_gap_ = 0, min_spare_ = 1e9;
    std::string waiting_title_;
    std::int64_t render_us_ = 0;
    std::uint64_t hole_frames_ = 0, holes_ = 0, last_read_ = 0;
    std::int64_t progress_elapsed_ms_ = -1, progress_track_ms_ = -1;
    Tags named_;
    std::vector<std::int16_t> block_ = std::vector<std::int16_t>(kBlock * 2, 0);
    std::vector<std::int16_t> scratch_ = std::vector<std::int16_t>(1024 * 2, 0);
    std::vector<std::int16_t> silence_;
    std::vector<std::uint8_t> frame_;
};
} // namespace

int main()
{
    std::signal(SIGPIPE, SIG_IGN);
    mkdir(kWorkDir, 0777);
    const std::string root = kWorkDir;
    g_log = std::fopen((root + "/music.log").c_str(), "a");
    say("=== music over games starting pid=%d", getpid());
    const int lock = open((root + "/lock").c_str(), O_RDWR | O_CREAT, 0666);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB))
    {
        say("another copy is running");
        return 1;
    }
    unlink((root + "/stop").c_str());
    Config config;
    if (!read_config(config))
    {
        say("no server given, and no usable %s/music.conf", kWorkDir);
        return 2;
    }
    ControlPort control;
    if (!control.start())
    {
        say("cannot open the control port errno=%d", errno);
        return 5;
    }
    hui::afterglow::Client client;
    // The console's Music level does the turning down, and a game is loud enough as it is.
    client.set_start_volume(100);
    Stream stream(config, client);
    if (!stream.can_watch(root + "/music.log"))
    {
        say("cannot follow the player on this firmware");
        show_toast(config, "Music over games", "Not supported on this system software");
        return 3;
    }
    if (!client.start(config.host.c_str(), static_cast<std::uint16_t>(config.port),
                      config.client_id.c_str(), config.name.c_str()))
    {
        say("client start failed");
        return 4;
    }
    const double started = static_cast<double>(now_us()) / 1e6;
    double next_status = started, previous_tick = started;
    for (;;)
    {
        const double now = static_cast<double>(now_us()) / 1e6;
        if (control.poll(stream.summary(), now))
        {
            say("stop requested by the app");
            break;
        }
        if (!access((root + "/stop").c_str(), F_OK))
        {
            say("stop requested");
            unlink((root + "/stop").c_str());
            break;
        }
        stream.tick(now);
        stream.note_gap(now - previous_tick);
        previous_tick = now;
        if (now >= next_status)
        {
            stream.status(now);
            rotate_log();
            next_status = now + config.status_every;
        }
        usleep(20000);
    }
    stream.finish();
    client.stop();
    client.join();
    say("=== exited cleanly");
    return 0;
}
