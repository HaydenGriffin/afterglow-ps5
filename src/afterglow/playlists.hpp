// Afterglow - Playlists in Music Assistant: save the queue, add a song, keep the programme.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/companion.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// LG Afterglow's playlists.js (spec: .lanes/_brief/R2-playlists.md). Music
// Assistant 2.10 owns playlists; Afterglow only asks. Names are made here so
// no keyboard is needed ("Queue · 16 Oct", unique with " (2)"), and the
// background task Music Assistant hands back is followed (tasks/get, every
// second for at most 30 s) until it says how many songs went in. One job at a
// time; its outcome is kept for the row that started it (confirmation()).
namespace hui::afterglow
{
// Local wall-clock time: utc for Music Assistant, local (utc shifted by the
// console's time zone) for dates and "21:04" labels.
struct LocalTime
{
    std::int64_t utc = 0, local = 0;
    int year = 0, month = 1, day = 1, hour = 0, minute = 0; // of local
};
LocalTime local_time_at(std::int64_t utc, std::int64_t local);
// The clock now; host builds use localtime_r, the console asks the kernel.
LocalTime local_time();

namespace playlist_wire
{
extern const char *const kMonths[12];
// "16 Oct": month 1..12.
std::string short_date(int day, int month);
std::string playlist_name(std::string_view prefix, int day, int month);
// The name when free, else the first of "name (2)" … "name (99)" that is.
std::string unique_name(const std::string &base, const std::vector<std::string> &taken);

struct Playlist
{
    std::string item_id, name;
};
// music/playlists/library_items: the ones a song can go into (is_editable,
// not dynamic), in the order asked, at most limit.
std::vector<Playlist> decode_editable(std::string_view result, std::size_t limit = 8);
std::vector<std::string> decode_names(std::string_view result);
// create_playlist's answer: the new playlist's library id ("" when none).
std::string created_id(std::string_view result);

enum class TaskState
{
    running,
    done,
    failed
};
struct Task
{
    std::string id;
    TaskState state = TaskState::done;
    int added = -1; // songs the task says it put in, -1 when it does not say
};
// A BackgroundTask; an answer that is no task (an older server) is done.
Task decode_task(std::string_view result);

// What the job did: "save" the queue, "keep" the programme, "add" songs.
enum class JobKind
{
    save,
    keep,
    add
};
enum class Verb
{
    save,
    create,
    add
};
// Music Assistant's refusals by error code (music_assistant_models.errors).
std::string refusal(int error_code, Verb verb);
struct Outcome
{
    std::string text;
    bool ok = false;
};
// expected: songs asked for; added -1 is taken as all of them.
Outcome outcome_text(JobKind kind, const std::string &name, int added, int expected);

// Request arguments (JSON objects).
std::string editable_args();
std::string name_check_args(std::string_view name);
std::string save_args(std::string_view queue_id, std::string_view name);
std::string create_args(std::string_view name);
std::string add_args(std::string_view playlist_id, const std::vector<std::string> &uris);
std::string task_args(std::string_view task_id);
} // namespace playlist_wire

// The picker's rows: New playlist first, then the list or why not.
struct PickerRow
{
    enum class Kind
    {
        create,
        playlist,
        retry,
        status
    } kind = Kind::status;
    std::string label, detail;
    playlist_wire::Playlist playlist;
    bool disabled = false;
};

class Playlists
{
  public:
    static constexpr std::int64_t kConfirmMs = 2500; // a success's tick (LG CONFIRM_MS)
    static constexpr std::int64_t kNoticeMs = 6000;  // anything else stays longer
    static constexpr std::int64_t kPollMs = 1000;
    static constexpr std::int64_t kTaskWaitMs = 30000;
    enum class ListState
    {
        idle,
        loading,
        ready,
        error
    };
    struct Job
    {
        std::string key, name, text;
        playlist_wire::JobKind kind = playlist_wire::JobKind::add;
        bool working = false, ok = false;
        std::int64_t at = 0;
    };
    struct Confirmation
    {
        std::string text;
        bool tick = false;
    };

    // Each returns false while another job runs. key names the row that confirms.
    bool save_queue(const std::string &queue_id, int expected, const LocalTime &now);
    bool add_to(const playlist_wire::Playlist &playlist, std::vector<std::string> uris,
                const std::string &key);
    bool add_to_new(std::vector<std::string> uris, const std::string &key, const LocalTime &now);
    bool keep(std::vector<std::string> uris, const std::string &prefix, const std::string &key,
              const LocalTime &now);
    // The playlists a song can go into, read afresh each time the picker opens.
    void load_editable();
    std::vector<PickerRow> picker_rows(const LocalTime &now) const;

    // Every frame: takes this service's replies and adds the calls it wants.
    void update(std::int64_t now_ms, std::vector<ApiReply> &replies, std::vector<ApiCall> &calls,
                std::uint64_t &next_id);
    // A finished job's words once, for a toast ("" when none).
    std::string take_notice()
    {
        return std::exchange(notice_, {});
    }
    const Job &job() const
    {
        return job_;
    }
    bool busy() const
    {
        return job_.working;
    }
    // The clock the last update() ran at, for confirmation().
    std::int64_t clock() const
    {
        return now_ms_;
    }
    ListState list_state() const
    {
        return list_state_;
    }
    const std::vector<playlist_wire::Playlist> &list() const
    {
        return list_;
    }
    // What the row with this key reads now; empty once it has gone.
    std::optional<Confirmation> confirmation(const std::string &key, std::int64_t now_ms) const;

  private:
    enum class Step
    {
        none,
        name_check,
        save,
        create,
        add,
        poll
    };
    bool begin(const std::string &key, playlist_wire::JobKind kind, std::string text,
               std::string base_name, Step first);
    void finish(std::string text, bool ok);
    void answer(const ApiReply &reply);
    void settle_task(const playlist_wire::Task &task);
    ApiCall next_call(std::uint64_t &next_id);

    Job job_;
    Step step_ = Step::none;
    bool step_sent_ = false;
    std::uint64_t waiting_ = 0; // the call this job waits for
    std::string queue_id_, playlist_id_, task_id_;
    std::vector<std::string> uris_;
    int expected_ = 0;
    std::int64_t now_ms_ = 0, task_started_ = 0, next_poll_ = 0;
    bool create_first_ = false; // a new playlist before the songs go in
    std::string notice_;
    ListState list_state_ = ListState::idle;
    std::vector<playlist_wire::Playlist> list_;
    bool list_wanted_ = false;
    std::uint64_t list_waiting_ = 0;
};
} // namespace hui::afterglow
