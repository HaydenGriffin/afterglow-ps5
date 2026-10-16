// Afterglow - Check my setup: a pass/fail list with one next step each (LG setup-check.js).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <pthread.h>
#include <string>
#include <string_view>
#include <vector>

namespace hui::afterglow
{
struct Presentation;

// The checks run only when asked (Settings, or the offer on an empty screen),
// never in a loop. They are read-only: one short API socket (greeting, auth,
// players/get), the relay's login with a player id nobody has (so no audio
// tap starts), one HTTPS request to lrclib.net when Online lyrics is on; the
// rest are facts the app already has. Rows and copy match LG Afterglow
// (.lanes/_brief/R2-share.md).
enum class SetupState
{
    pass,
    fail,
    busy,     // online lyrics only: LRCLIB answered 429 or 5xx
    skip,     // an earlier check it depends on failed: "Not checked"
    checking, // the probes are under way
};
struct SetupRow
{
    std::string id, label, value, next;
    SetupState state = SetupState::checking;
};

// What the app already knows when a check starts.
struct SetupFacts
{
    bool configured = false;   // a Music Assistant host and token
    bool config_token = false; // the token is afterglow.conf's, not a sign-in
    std::string host;          // as shown in the next step ("192.168.1.10:8095")
    bool plays = false;        // Source is This PS5
    bool sendspin = false;     // the Sendspin client is connected
    bool follows = false;      // Source follows a speaker (the relay is used)
    bool online_lyrics = false;
    int presets = 0, measured = 0; // presets found, and how many have a measurement
    std::int64_t now_s = 0;        // the console's clock, UTC seconds
};
// What the probes found.
struct SetupResults
{
    bool reached = false;
    std::string version;
    bool authed = false, player_found = false, relay_ok = false;
    std::int64_t server_s = 0; // the server's Date, UTC seconds; 0 when it sent none
    int lyrics_status = 0;     // HTTP status from lrclib.net; 0 for no answer
};

// "2.10.6" passes, "2.9.3" does not; "0.0.0", a development build, passes.
bool setup_version_ok(std::string_view version);
// An LRCLIB status: 429 and 5xx are busy, any other answer passes, 0 fails.
SetupState setup_lyrics_state(int status);
// RFC 7231 IMF-fixdate ("Fri, 16 Oct 2026 14:49:13 GMT") to UTC seconds; 0 when malformed.
std::int64_t parse_http_date(std::string_view text);
// More than this apart and the Clock row fails.
inline constexpr std::int64_t kClockSlackSeconds = 300;
// How far the console's clock is off: "12 min behind", "3 h 12 min ahead", "2 days behind".
std::string clock_offset_label(std::int64_t console_minus_server_s);

// The facts from the model (main.cpp keeps model.connected on the Sendspin
// client while Source is This PS5) and the console's clock. host is what the
// next step names; config_token: the token is afterglow.conf's.
SetupFacts setup_facts(const Presentation &model, std::string host, bool config_token);

// The rows from the facts and, once they are in, the results (null while running).
std::vector<SetupRow> setup_rows(const SetupFacts &facts, const SetupResults *results);
// "Checking…", "Everything looks right.", "1 thing needs a look." (busy is not counted).
std::string setup_note(const std::vector<SetupRow> &rows);
// Settings' value: "Not run" before any check, then "All good" or "n to look at".
std::string setup_summary(const std::vector<SetupRow> &rows, bool has_run);
// The host preview's fixed answers (Music Assistant 2.10.6, lyrics busy, no scan).
std::vector<SetupRow> preview_setup_rows();

// What one run talks to.
struct SetupRequest
{
    std::string host;
    std::uint16_t port = 8095;
    std::string token;
    // This PS5's Music Assistant player, by any id it may have (client_id,
    // ma_player_id, the player named as this PS5); empty when it does not play.
    std::vector<std::string> player_ids;
    bool lyrics = false;
};
// The probes, injectable for tests; each blocks and is bounded.
struct SetupProbes
{
    // Fills reached, version, authed, player_found and server_s.
    std::function<void(const SetupRequest &, SetupResults &, std::atomic<bool> &)> server;
    std::function<bool(const SetupRequest &, std::atomic<bool> &)> relay;
    std::function<int(std::atomic<bool> &)> lyrics;
};
// The production server and relay probes (ma_socket); lyrics comes from main.cpp,
// beside the HTTPS client.
SetupProbes setup_socket_probes();
// The player id the relay check asks for: nobody has it, so no tap starts.
inline constexpr const char *kRelayProbePlayer = "afterglow-setup-check";

// One run at a time on its own thread, never on the rendering or audio threads.
class SetupCheck
{
  public:
    explicit SetupCheck(SetupProbes probes);
    ~SetupCheck();
    SetupCheck(const SetupCheck &) = delete;
    SetupCheck &operator=(const SetupCheck &) = delete;
    // False while a run is under way.
    bool run(SetupRequest request);
    bool take(SetupResults &results);
    bool busy() const
    {
        return busy_.load();
    }
    void stop();
    void join();

  private:
    static void *entry(void *self);
    void work();
    SetupProbes probes_;
    SetupRequest request_;
    SetupResults results_;
    std::mutex mutex_;
    pthread_t thread_{};
    bool started_ = false, ready_ = false;
    std::atomic<bool> busy_{false}, stopping_{false};
};

// Once per status tick on the UI thread: starts a run when the UI asked
// (presentation.setup_requested) and puts the rows in presentation.setup_rows.
void pump_setup(Presentation &model, SetupCheck &check, const SetupFacts &facts,
                const SetupRequest &request);
} // namespace hui::afterglow
