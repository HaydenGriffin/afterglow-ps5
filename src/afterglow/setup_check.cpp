// Afterglow - Check my setup: a pass/fail list with one next step each (LG setup-check.js).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/setup_check.hpp"
#include "afterglow/ma_socket.hpp"
#include "afterglow/presentation.hpp"
#include "afterglow/protocol.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <poll.h>
#include <ctime>
#include <utility>

namespace hui::afterglow
{
namespace
{
SetupRow row(const char *id, const char *label, SetupState state, std::string value,
             std::string next = {})
{
    SetupRow out;
    out.id = id;
    out.label = label;
    out.state = state;
    out.value = std::move(value);
    out.next = std::move(next);
    return out;
}
SetupRow checking(const char *id, const char *label)
{
    return row(id, label, SetupState::checking, "Checking…");
}
SetupRow not_checked(const char *id, const char *label)
{
    return row(id, label, SetupState::skip, "Not checked");
}
int failures(const std::vector<SetupRow> &rows)
{
    return static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const SetupRow &r)
                                          { return r.state == SetupState::fail; }));
}
bool running(const std::vector<SetupRow> &rows)
{
    return std::any_of(rows.begin(), rows.end(),
                       [](const SetupRow &r) { return r.state == SetupState::checking; });
}
// Days since 1970-01-01 for a civil date (Howard Hinnant's days_from_civil).
std::int64_t days_from_civil(std::int64_t y, int m, int d)
{
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy =
        (153 * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}
} // namespace

bool setup_version_ok(std::string_view version)
{
    int parts[3] = {-1, -1, 0};
    int count = 0;
    std::size_t at = 0;
    while (count < 3 && at < version.size())
    {
        if (version[at] < '0' || version[at] > '9')
            break;
        int value = 0;
        while (at < version.size() && version[at] >= '0' && version[at] <= '9' && value < 100000)
            value = value * 10 + (version[at++] - '0');
        parts[count++] = value;
        if (at < version.size() && version[at] == '.')
            ++at;
        else
            break;
    }
    if (count < 2)
        return false;
    if (parts[0] == 0 && parts[1] == 0 && parts[2] == 0)
        return true;
    return parts[0] > 2 || (parts[0] == 2 && parts[1] >= 10);
}

SetupState setup_lyrics_state(int status)
{
    if (status <= 0)
        return SetupState::fail;
    return status == 429 || status >= 500 ? SetupState::busy : SetupState::pass;
}

std::int64_t parse_http_date(std::string_view text)
{
    // "Fri, 16 Oct 2026 14:49:13 GMT"
    constexpr std::string_view kMonths = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const auto comma = text.find(", ");
    if (comma == std::string_view::npos || text.size() < comma + 26)
        return 0;
    const std::string rest(text.substr(comma + 2, 24));
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    char month[4] = {};
    char zone[4] = {};
    if (std::sscanf(rest.c_str(), "%2d %3c %4d %2d:%2d:%2d %3c", &day, month, &year, &hour, &minute,
                    &second, zone) != 7 ||
        std::string_view(zone, 3) != "GMT")
        return 0;
    const auto found = kMonths.find(std::string_view(month, 3));
    if (found == std::string_view::npos || found % 3 != 0 || day < 1 || day > 31 || hour > 23 ||
        minute > 59 || second > 60 || year < 1970)
        return 0;
    const int m = static_cast<int>(found / 3) + 1;
    return days_from_civil(year, m, day) * 86400 + hour * 3600 + minute * 60 + second;
}

std::string clock_offset_label(std::int64_t offset)
{
    const char *side = offset < 0 ? " behind" : " ahead";
    const std::int64_t seconds = std::llabs(offset);
    const std::int64_t minutes = (seconds + 30) / 60;
    if (minutes < 60)
        return std::to_string(std::max<std::int64_t>(minutes, 1)) + " min" + side;
    if (seconds < 2 * 86400)
    {
        const std::int64_t rest = minutes % 60;
        return std::to_string(minutes / 60) + " h" +
               (rest ? " " + std::to_string(rest) + " min" : std::string{}) + side;
    }
    return std::to_string(seconds / 86400) + " days" + side;
}

std::vector<SetupRow> setup_rows(const SetupFacts &facts, const SetupResults *results)
{
    std::vector<SetupRow> rows;
    const bool run = results == nullptr;
    const bool reached = !run && results->reached;
    const bool authed = reached && results->authed;

    if (!facts.configured)
        rows.push_back(row("server", "Music Assistant", SetupState::fail, "Not set up",
                           "Sign in under Settings, Account."));
    else if (run)
        rows.push_back(checking("server", "Music Assistant"));
    else if (!reached)
        rows.push_back(row("server", "Music Assistant", SetupState::fail, "Can’t reach it",
                           "Check that Music Assistant is running at " + facts.host + "."));
    else if (!setup_version_ok(results->version))
        rows.push_back(row("server", "Music Assistant", SetupState::fail,
                           "Version " + results->version,
                           "Update Music Assistant to 2.10 or newer."));
    else
        rows.push_back(
            row("server", "Music Assistant", SetupState::pass, "Version " + results->version));

    if (facts.configured && run)
        rows.push_back(checking("account", "Signed in"));
    else if (!reached)
        rows.push_back(not_checked("account", "Signed in"));
    else if (!authed)
        rows.push_back(row("account", "Signed in", SetupState::fail, "Token refused",
                           facts.config_token ? "Put a new ma_token in afterglow.conf, or sign in "
                                                "under Settings, Account."
                                              : "Sign in again under Settings, Account."));
    else
        rows.push_back(row("account", "Signed in", SetupState::pass, "Token accepted"));

    if (facts.plays)
    {
        const char *label = "This PS5 as a speaker";
        if (facts.configured && run)
            rows.push_back(checking("player", label));
        else if (!authed)
            rows.push_back(not_checked("player", label));
        else if (!results->player_found)
            rows.push_back(row("player", label, SetupState::fail, "Not in Music Assistant",
                               "Check that Music Assistant’s Sendspin provider is on, then reopen "
                               "Afterglow."));
        else
            rows.push_back(row("player", label, SetupState::pass, "In Music Assistant"));
        rows.push_back(facts.sendspin
                           ? row("sendspin", "Sendspin", SetupState::pass, "Connected")
                           : row("sendspin", "Sendspin", SetupState::fail, "Not connected",
                                 "Check that Music Assistant’s Sendspin provider is on."));
    }

    if (facts.follows)
    {
        if (facts.configured && run)
            rows.push_back(checking("visualizer", "MilkDrop Visualizer"));
        else if (!authed)
            rows.push_back(not_checked("visualizer", "MilkDrop Visualizer"));
        else if (!results->relay_ok)
            rows.push_back(row("visualizer", "MilkDrop Visualizer", SetupState::fail,
                               "Plugin not found",
                               "Add MilkDrop Visualizer in Music Assistant: Settings, Plugins."));
        else
            rows.push_back(
                row("visualizer", "MilkDrop Visualizer", SetupState::pass, "Plugin found"));
    }

    if (facts.configured && run)
        rows.push_back(checking("clock", "Clock"));
    else if (!reached || results->server_s <= 0)
        rows.push_back(not_checked("clock", "Clock"));
    else if (std::llabs(facts.now_s - results->server_s) > kClockSlackSeconds)
        rows.push_back(row("clock", "Clock", SetupState::fail,
                           clock_offset_label(facts.now_s - results->server_s),
                           "Set the date and time in the PS5’s settings. Online lyrics need it."));
    else
        rows.push_back(row("clock", "Clock", SetupState::pass, "Right"));

    if (facts.online_lyrics)
    {
        if (run)
            rows.push_back(checking("lyrics", "Online lyrics"));
        else
            switch (setup_lyrics_state(results->lyrics_status))
            {
            case SetupState::pass:
                rows.push_back(
                    row("lyrics", "Online lyrics", SetupState::pass, "lrclib.net answers"));
                break;
            case SetupState::busy:
                rows.push_back(row("lyrics", "Online lyrics", SetupState::busy, "Busy right now",
                                   "LRCLIB is busy. Lyrics are tried again by themselves."));
                break;
            default:
                rows.push_back(
                    row("lyrics", "Online lyrics", SetupState::fail, "Can’t reach lrclib.net",
                        "Check the PS5’s internet connection, or turn Online lyrics off."));
                break;
            }
    }

    if (facts.presets > 0)
    {
        if (facts.measured >= facts.presets)
            rows.push_back(row("scan", "Preset scan", SetupState::pass, "Done"));
        else
            rows.push_back(row("scan", "Preset scan", SetupState::fail,
                               facts.measured > 0 ? std::to_string(facts.measured) + " of " +
                                                        std::to_string(facts.presets) + " measured"
                                                  : "Not run",
                               "Run Measure preset performance in Settings, Visuals & presets."));
    }
    return rows;
}

std::string setup_note(const std::vector<SetupRow> &rows)
{
    if (rows.empty())
        return {};
    if (running(rows))
        return "Checking…";
    const int failed = failures(rows);
    return failed == 0   ? "Everything looks right."
           : failed == 1 ? "1 thing needs a look."
                         : std::to_string(failed) + " things need a look.";
}

std::string setup_summary(const std::vector<SetupRow> &rows, bool has_run)
{
    if (!has_run)
        return "Not run";
    if (running(rows))
        return "Checking…";
    const int failed = failures(rows);
    return failed ? std::to_string(failed) + " to look at" : "All good";
}

SetupFacts setup_facts(const Presentation &model, std::string host, bool config_token)
{
    SetupFacts facts;
    facts.configured = model.companion_configured;
    facts.config_token = config_token;
    facts.host = std::move(host);
    facts.plays = !model.preferences.follow;
    facts.sendspin = facts.plays && model.connected;
    facts.follows = model.preferences.follow;
    facts.online_lyrics = model.preferences.online_lyrics;
    facts.presets = static_cast<int>(model.presets.size());
    const auto &ratings = model.quality.ratings();
    for (const auto &name : model.presets)
    {
        const auto found = ratings.find(name);
        if (found != ratings.end() && (found->second.measured || found->second.quarantined))
            ++facts.measured;
    }
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) == 0)
        facts.now_s = now.tv_sec;
    return facts;
}

std::vector<SetupRow> preview_setup_rows()
{
    SetupFacts facts;
    facts.configured = true;
    facts.host = "music.local:8095";
    facts.plays = true;
    facts.sendspin = true;
    facts.online_lyrics = true;
    facts.presets = 393;
    facts.now_s = 1792161000;
    SetupResults results;
    results.reached = results.authed = results.player_found = results.relay_ok = true;
    results.version = "2.10.6";
    results.server_s = facts.now_s + 4;
    results.lyrics_status = 503;
    return setup_rows(facts, &results);
}

namespace
{
// One short API socket: greeted, then a command at a time; every wait bounded.
class Probe
{
  public:
    explicit Probe(std::atomic<bool> &stop) : stopping_(stop), socket_(stop)
    {
        socket_.handler = [this](int opcode, const std::uint8_t *data, std::size_t size)
        {
            if (opcode == 1 && inbox_.size() < 64)
                inbox_.emplace_back(reinterpret_cast<const char *>(data), size);
            return true;
        };
    }
    bool open(const std::string &host, std::uint16_t port, const std::string &path)
    {
        return socket_.open(host, port, path);
    }
    const std::string &date() const
    {
        return socket_.server_date;
    }
    bool send(const std::string &text)
    {
        return socket_.send_text(text);
    }
    // The next text message for which want() is true; "" after the wait or a close.
    std::string wait(const std::function<bool(const std::string &)> &want, int ms)
    {
        const auto deadline = sys::monotonic_us() + static_cast<std::int64_t>(ms) * 1000;
        while (!stopping_ && sys::monotonic_us() < deadline)
        {
            if (!socket_.tick())
                return {};
            while (!inbox_.empty())
            {
                std::string message = std::move(inbox_.front());
                inbox_.pop_front();
                if (want(message))
                    return message;
            }
            poll(nullptr, 0, 10);
        }
        return {};
    }

  private:
    std::atomic<bool> &stopping_;
    ma_socket::Socket socket_;
    std::deque<std::string> inbox_;
};

std::string command(const char *id, const char *name, const std::string &args)
{
    return std::string("{\"message_id\":\"") + id + "\",\"command\":\"" + name +
           "\",\"args\":" + args + "}";
}
// The value after "key": in a message, as written ("auth", true, null).
// A plain scan, not a parse: the auth reply carries the user's preferences,
// far beyond Json's token budget, and these keys appear once, near the top.
std::string_view value_of(std::string_view message, std::string_view key)
{
    const std::string quoted = "\"" + std::string(key) + "\"";
    auto at = message.find(quoted);
    if (at == std::string_view::npos)
        return {};
    at += quoted.size();
    while (at < message.size() && (message[at] == ' ' || message[at] == ':'))
        ++at;
    if (at < message.size() && message[at] == '"')
    {
        const auto end = message.find('"', at + 1);
        return end == std::string_view::npos ? std::string_view{}
                                             : message.substr(at + 1, end - at - 1);
    }
    auto end = at;
    while (end < message.size() && std::strchr(",}] \n", message[end]) == nullptr)
        ++end;
    return message.substr(at, end - at);
}
bool is_reply(const std::string &message, const char *id)
{
    return value_of(message, "message_id") == id;
}

void probe_server(const SetupRequest &request, SetupResults &out, std::atomic<bool> &stop)
{
    Probe probe(stop);
    if (!probe.open(request.host, request.port, "/ws"))
        return;
    const auto greeting = probe.wait([](const std::string &m)
                                     { return !value_of(m, "server_version").empty(); }, 8000);
    if (greeting.empty())
        return;
    out.reached = true;
    out.version = std::string(value_of(greeting, "server_version"));
    out.server_s = parse_http_date(probe.date());
    if (!probe.send(command("auth", "auth", "{\"token\":" + json_quote(request.token) + "}")))
        return;
    const auto auth = probe.wait([](const std::string &m) { return is_reply(m, "auth"); }, 8000);
    out.authed = value_of(auth, "error_code").empty() && value_of(auth, "authenticated") == "true";
    for (std::size_t i = 0; out.authed && !out.player_found && i < request.player_ids.size(); ++i)
    {
        const auto &id = request.player_ids[i];
        if (id.empty() ||
            !probe.send(command("player", "players/get", "{\"player_id\":" + json_quote(id) + "}")))
            continue;
        const auto answer =
            probe.wait([](const std::string &m) { return is_reply(m, "player"); }, 8000);
        // A player Music Assistant does not know comes back as a null result.
        out.player_found =
            value_of(answer, "result") != "null" && value_of(answer, "player_id") == id;
    }
}

bool probe_relay(const SetupRequest &request, std::atomic<bool> &stop)
{
    Probe probe(stop);
    if (!probe.open(request.host, request.port,
                    std::string("/milkdrop_visualizer?player=") + kRelayProbePlayer))
        return false;
    if (!probe.send("{\"type\":\"auth\",\"token\":" + json_quote(request.token) + "}"))
        return false;
    return !probe.wait([](const std::string &m) { return value_of(m, "type") == "auth_ok"; }, 8000)
                .empty();
}
} // namespace

SetupProbes setup_socket_probes()
{
    SetupProbes probes;
    probes.server = probe_server;
    probes.relay = probe_relay;
    return probes;
}

SetupCheck::SetupCheck(SetupProbes probes) : probes_(std::move(probes))
{
}
SetupCheck::~SetupCheck()
{
    stop();
    join();
}

bool SetupCheck::run(SetupRequest request)
{
    if (busy_.exchange(true))
        return false;
    join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request_ = std::move(request);
        ready_ = false;
    }
    stopping_ = false;
    started_ = pthread_create(&thread_, nullptr, &SetupCheck::entry, this) == 0;
    if (!started_)
        busy_ = false;
    return started_;
}

bool SetupCheck::take(SetupResults &results)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_)
        return false;
    ready_ = false;
    results = results_;
    return true;
}

void SetupCheck::stop()
{
    stopping_ = true;
}

void SetupCheck::join()
{
    if (started_)
    {
        pthread_join(thread_, nullptr);
        started_ = false;
    }
}

void *SetupCheck::entry(void *self)
{
    static_cast<SetupCheck *>(self)->work();
    return nullptr;
}

void SetupCheck::work()
{
    SetupRequest request;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request = request_;
    }
    SetupResults results;
    if (probes_.server && !request.host.empty())
        probes_.server(request, results, stopping_);
    if (results.authed && probes_.relay)
        results.relay_ok = probes_.relay(request, stopping_);
    if (request.lyrics && probes_.lyrics)
        results.lyrics_status = probes_.lyrics(stopping_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        results_ = results;
        ready_ = true;
    }
    busy_ = false;
}

void pump_setup(Presentation &model, SetupCheck &check, const SetupFacts &facts,
                const SetupRequest &request)
{
    if (model.setup_requested && !check.busy())
    {
        model.setup_requested = false;
        model.setup_has_run = true;
        model.setup_rows = setup_rows(facts, nullptr);
        // Without a server only lrclib.net is asked; the first row says so at once.
        auto ask = request;
        if (!facts.configured)
            ask.host.clear();
        if (!check.run(std::move(ask)))
        {
            const SetupResults none;
            model.setup_rows = setup_rows(facts, &none);
        }
    }
    SetupResults results;
    if (check.take(results))
        model.setup_rows = setup_rows(facts, &results);
}
} // namespace hui::afterglow
