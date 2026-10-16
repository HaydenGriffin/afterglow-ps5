// Afterglow - Playlists in Music Assistant: save the queue, add a song, keep the programme.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/playlists.hpp"
#include "afterglow/json_slice.hpp"
#include <algorithm>
#include <ctime>

#ifdef __PROSPERO__
// As night.cpp: the kernel applies the console's time zone setting.
extern "C" int sceKernelConvertUtcToLocaltime(time_t utc, time_t *local, void *zone,
                                              int *dst_seconds);
#endif

namespace hui::afterglow
{
namespace js = json_slice;

LocalTime local_time_at(std::int64_t utc, std::int64_t local)
{
    LocalTime out;
    out.utc = utc;
    out.local = local;
    const auto seconds = static_cast<time_t>(local);
    std::tm parts{};
    if (gmtime_r(&seconds, &parts) == nullptr)
        return out;
    out.year = parts.tm_year + 1900;
    out.month = parts.tm_mon + 1;
    out.day = parts.tm_mday;
    out.hour = parts.tm_hour;
    out.minute = parts.tm_min;
    return out;
}

LocalTime local_time()
{
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return {};
#ifdef __PROSPERO__
    time_t local = 0;
    int zone[2] = {}, dst = 0;
    if (sceKernelConvertUtcToLocaltime(now.tv_sec, &local, zone, &dst) != 0)
        local = now.tv_sec;
    return local_time_at(now.tv_sec, local);
#else
    std::tm parts{};
    if (localtime_r(&now.tv_sec, &parts) == nullptr)
        return local_time_at(now.tv_sec, now.tv_sec);
    return local_time_at(now.tv_sec, now.tv_sec + parts.tm_gmtoff);
#endif
}

namespace playlist_wire
{
const char *const kMonths[12] = {"January",   "February", "March",    "April",
                                 "May",       "June",     "July",     "August",
                                 "September", "October",  "November", "December"};

std::string short_date(int day, int month)
{
    const int m = std::clamp(month, 1, 12) - 1;
    return std::to_string(day) + " " + std::string(kMonths[m]).substr(0, 3);
}

std::string playlist_name(std::string_view prefix, int day, int month)
{
    return std::string(prefix) + " · " + short_date(day, month);
}

std::string unique_name(const std::string &base, const std::vector<std::string> &taken)
{
    const auto used = [&](const std::string &name)
    { return std::find(taken.begin(), taken.end(), name) != taken.end(); };
    if (!used(base))
        return base;
    for (int n = 2; n < 100; ++n)
    {
        const auto name = base + " (" + std::to_string(n) + ")";
        if (!used(name))
            return name;
    }
    return base;
}

namespace
{
// Library ids arrive as strings, older servers sent numbers.
std::string id_of(std::string_view item)
{
    auto id = js::text(item, "item_id");
    if (id.empty())
    {
        const auto number = js::integer(item, "item_id", -1);
        if (number >= 0)
            id = std::to_string(number);
    }
    return id;
}
} // namespace

std::vector<Playlist> decode_editable(std::string_view result, std::size_t limit)
{
    std::vector<Playlist> out;
    for (auto item : js::elements(result, 200))
    {
        if (out.size() >= limit)
            break;
        if (js::field(item, "is_editable") != "true" || js::field(item, "is_dynamic") == "true")
            continue;
        Playlist playlist;
        playlist.item_id = id_of(item);
        playlist.name = js::text(item, "name");
        if (!playlist.item_id.empty() && !playlist.name.empty())
            out.push_back(std::move(playlist));
    }
    return out;
}

std::vector<std::string> decode_names(std::string_view result)
{
    std::vector<std::string> names;
    for (auto item : js::elements(result, 200))
        names.push_back(js::text(item, "name"));
    return names;
}

std::string created_id(std::string_view result)
{
    return id_of(result);
}

Task decode_task(std::string_view result)
{
    Task task;
    task.id = js::text(result, "id");
    const auto status = js::text(result, "status");
    if (task.id.empty() || status.empty() || status == "success" || status == "partial_success")
        task.state = TaskState::done;
    else if (status == "failed" || status == "cancelled")
        task.state = TaskState::failed;
    else
        task.state = TaskState::running;
    const auto text = js::text(result, "progress_text");
    constexpr std::string_view kAdded = "Added ";
    if (text.starts_with(kAdded))
    {
        std::size_t at = kAdded.size(), digits = 0;
        int added = 0;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9' && digits < 7)
        {
            added = added * 10 + (text[at++] - '0');
            ++digits;
        }
        if (digits > 0 && text.substr(at).starts_with(" item"))
            task.added = added;
    }
    else if (text.starts_with("No new playlist items"))
        task.added = 0;
    return task;
}

std::string refusal(int error_code, Verb verb)
{
    if (error_code == 22 || error_code == 18)
        return "This account can’t change playlists";
    if (verb == Verb::save && error_code == 8)
        return "Nothing in the queue can go in a playlist";
    if (verb != Verb::add && (error_code == 1 || error_code == 3))
        return "Music Assistant has nowhere to keep playlists";
    return verb == Verb::add ? "Couldn’t add. Try again" : "Couldn’t save. Try again";
}

Outcome outcome_text(JobKind kind, const std::string &name, int added, int expected)
{
    const std::string quoted = "“" + name + "”";
    if (kind == JobKind::add)
    {
        if (added == 0)
            return {"Already in " + quoted + ", or not available there", false};
        return {"Added to " + quoted, true};
    }
    const std::string done = (kind == JobKind::keep ? "Kept as " : "Saved as ") + quoted;
    const int missing = added < 0 ? 0 : std::max(0, expected - added);
    if (missing > 0)
        return {done + " · " + std::to_string(missing) + (missing == 1 ? " song" : " songs") +
                    " couldn’t be added",
                false};
    return {done, true};
}

std::string editable_args()
{
    return R"({"limit":50,"offset":0,"order_by":"timestamp_modified_desc"})";
}
std::string name_check_args(std::string_view name)
{
    return "{\"search\":" + js::quote(name) + ",\"limit\":50,\"offset\":0}";
}
std::string save_args(std::string_view queue_id, std::string_view name)
{
    return "{\"queue_id\":" + js::quote(queue_id) + ",\"name\":" + js::quote(name) + "}";
}
std::string create_args(std::string_view name)
{
    return "{\"name\":" + js::quote(name) + "}";
}
std::string add_args(std::string_view playlist_id, const std::vector<std::string> &uris)
{
    std::string list;
    for (const auto &uri : uris)
        list += (list.empty() ? "" : ",") + js::quote(uri);
    return "{\"db_playlist_id\":" + js::quote(playlist_id) + ",\"uris\":[" + list + "]}";
}
std::string task_args(std::string_view task_id)
{
    return "{\"task_id\":" + js::quote(task_id) + "}";
}
} // namespace playlist_wire

namespace pw = playlist_wire;

bool Playlists::begin(const std::string &key, pw::JobKind kind, std::string text,
                      std::string base_name, Step first)
{
    if (job_.working)
        return false;
    job_ = {};
    job_.key = key;
    job_.kind = kind;
    job_.name = std::move(base_name);
    job_.text = std::move(text);
    job_.working = true;
    job_.at = now_ms_;
    step_ = first;
    step_sent_ = false;
    waiting_ = 0;
    task_id_.clear();
    return true;
}

bool Playlists::save_queue(const std::string &queue_id, int expected, const LocalTime &now)
{
    if (queue_id.empty() ||
        !begin("queue-save", pw::JobKind::save, "Saving…",
               pw::playlist_name("Queue", now.day, now.month), Step::name_check))
        return false;
    queue_id_ = queue_id;
    expected_ = expected;
    create_first_ = false;
    return true;
}

bool Playlists::add_to(const pw::Playlist &playlist, std::vector<std::string> uris,
                       const std::string &key)
{
    if (uris.empty() || !begin(key, pw::JobKind::add, "Adding to “" + playlist.name + "”…",
                               playlist.name, Step::add))
        return false;
    playlist_id_ = playlist.item_id;
    expected_ = static_cast<int>(uris.size());
    uris_ = std::move(uris);
    create_first_ = false;
    return true;
}

bool Playlists::add_to_new(std::vector<std::string> uris, const std::string &key,
                           const LocalTime &now)
{
    const auto name = pw::playlist_name("Playlist", now.day, now.month);
    if (uris.empty() ||
        !begin(key, pw::JobKind::add, "Adding to “" + name + "”…", name, Step::name_check))
        return false;
    expected_ = static_cast<int>(uris.size());
    uris_ = std::move(uris);
    create_first_ = true;
    return true;
}

bool Playlists::keep(std::vector<std::string> uris, const std::string &prefix,
                     const std::string &key, const LocalTime &now)
{
    if (uris.empty() || !begin(key, pw::JobKind::keep, "Keeping…",
                               pw::playlist_name(prefix, now.day, now.month), Step::name_check))
        return false;
    expected_ = static_cast<int>(uris.size());
    uris_ = std::move(uris);
    create_first_ = true;
    return true;
}

void Playlists::load_editable()
{
    list_state_ = ListState::loading;
    list_.clear();
    list_wanted_ = true;
    list_waiting_ = 0;
}

std::vector<PickerRow> Playlists::picker_rows(const LocalTime &now) const
{
    std::vector<PickerRow> rows;
    PickerRow create;
    create.kind = PickerRow::Kind::create;
    create.label = "New playlist";
    create.detail = "As “" + pw::playlist_name("Playlist", now.day, now.month) + "”";
    rows.push_back(create);
    PickerRow status;
    status.disabled = true;
    if (list_state_ == ListState::loading || list_state_ == ListState::idle)
        status.label = "Loading playlists…";
    else if (list_state_ == ListState::error)
    {
        status.kind = PickerRow::Kind::retry;
        status.label = "Couldn’t load playlists";
        status.detail = "Try again";
        status.disabled = false;
    }
    else if (list_.empty())
        status.label = "No playlists you can add to yet";
    else
    {
        for (const auto &playlist : list_)
        {
            PickerRow row;
            row.kind = PickerRow::Kind::playlist;
            row.label = playlist.name;
            row.playlist = playlist;
            rows.push_back(row);
        }
        return rows;
    }
    rows.push_back(status);
    return rows;
}

void Playlists::finish(std::string text, bool ok)
{
    job_.working = false;
    job_.text = std::move(text);
    job_.ok = ok;
    job_.at = now_ms_;
    step_ = Step::none;
    waiting_ = 0;
    notice_ = job_.text;
}

void Playlists::settle_task(const pw::Task &task)
{
    if (task.state == pw::TaskState::failed)
    {
        finish(job_.kind == pw::JobKind::add ? "Couldn’t add. Try again"
                                             : "Couldn’t save. Try again",
               false);
        return;
    }
    if (task.state == pw::TaskState::running)
    {
        if (now_ms_ - task_started_ >= kTaskWaitMs)
        {
            finish("Still saving in Music Assistant", false);
            return;
        }
        task_id_ = task.id;
        step_ = Step::poll;
        step_sent_ = false;
        next_poll_ = now_ms_ + kPollMs;
        return;
    }
    const auto out = pw::outcome_text(job_.kind, job_.name, task.added, expected_);
    finish(out.text, out.ok);
}

void Playlists::answer(const ApiReply &reply)
{
    const bool failed = reply.error_code != 0;
    switch (step_)
    {
    case Step::name_check:
        if (!failed)
            job_.name = pw::unique_name(job_.name, pw::decode_names(reply.result));
        step_ = create_first_ ? Step::create : Step::save;
        break;
    case Step::save:
        if (failed)
            return finish(pw::refusal(reply.error_code, pw::Verb::save), false);
        task_started_ = now_ms_;
        return settle_task(pw::decode_task(reply.result));
    case Step::create:
        if (failed)
            return finish(pw::refusal(reply.error_code, pw::Verb::create), false);
        playlist_id_ = pw::created_id(reply.result);
        if (playlist_id_.empty())
            return finish(pw::refusal(0, pw::Verb::create), false);
        step_ = Step::add;
        break;
    case Step::add:
        if (failed)
            return finish(pw::refusal(reply.error_code, pw::Verb::add), false);
        task_started_ = now_ms_;
        return settle_task(pw::decode_task(reply.result));
    case Step::poll:
        // A server that will not say (no tasks/get, or no scope): it is done.
        if (failed)
            return settle_task({});
        return settle_task(pw::decode_task(reply.result));
    case Step::none:
        return;
    }
    step_sent_ = false;
}

ApiCall Playlists::next_call(std::uint64_t &next_id)
{
    ApiCall call;
    call.id = ++next_id;
    switch (step_)
    {
    case Step::name_check:
        call.command = "music/playlists/library_items";
        call.args = pw::name_check_args(job_.name);
        break;
    case Step::save:
        call.command = "player_queues/save_as_playlist";
        call.args = pw::save_args(queue_id_, job_.name);
        break;
    case Step::create:
        call.command = "music/playlists/create_playlist";
        call.args = pw::create_args(job_.name);
        break;
    case Step::add:
        call.command = "music/playlists/add_playlist_tracks";
        call.args = pw::add_args(playlist_id_, uris_);
        break;
    case Step::poll:
        call.command = "tasks/get";
        call.args = pw::task_args(task_id_);
        break;
    case Step::none:
        break;
    }
    return call;
}

void Playlists::update(std::int64_t now_ms, std::vector<ApiReply> &replies,
                       std::vector<ApiCall> &calls, std::uint64_t &next_id)
{
    now_ms_ = now_ms;
    for (auto it = replies.begin(); it != replies.end();)
    {
        if (list_waiting_ != 0 && it->id == list_waiting_)
        {
            list_waiting_ = 0;
            list_state_ = it->error_code != 0 ? ListState::error : ListState::ready;
            list_ =
                it->error_code != 0 ? std::vector<pw::Playlist>{} : pw::decode_editable(it->result);
            it = replies.erase(it);
        }
        else if (waiting_ != 0 && it->id == waiting_)
        {
            const ApiReply reply = std::move(*it);
            it = replies.erase(it);
            waiting_ = 0;
            answer(reply);
        }
        else
            ++it;
    }
    if (list_wanted_)
    {
        list_wanted_ = false;
        ApiCall call;
        call.id = ++next_id;
        call.command = "music/playlists/library_items";
        call.args = pw::editable_args();
        list_waiting_ = call.id;
        calls.push_back(std::move(call));
    }
    if (job_.working && step_ != Step::none && !step_sent_ &&
        (step_ != Step::poll || now_ms >= next_poll_))
    {
        auto call = next_call(next_id);
        waiting_ = call.id;
        step_sent_ = true;
        calls.push_back(std::move(call));
    }
}

std::optional<Playlists::Confirmation> Playlists::confirmation(const std::string &key,
                                                               std::int64_t now_ms) const
{
    if (job_.key.empty() || job_.key != key)
        return std::nullopt;
    if (job_.working)
        return Confirmation{job_.text, false};
    if (now_ms - job_.at >= (job_.ok ? kConfirmMs : kNoticeMs))
        return std::nullopt;
    return Confirmation{job_.text, job_.ok};
}
} // namespace hui::afterglow
