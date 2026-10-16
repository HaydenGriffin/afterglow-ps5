// Afterglow - Music Assistant sign-in, token renewal and settings mirroring.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/preferences.hpp"
#include "afterglow/presentation.hpp"
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
// A token this PS5 obtained by signing in, kept in /download0 (/app0 is read-only).
struct StoredAuth
{
    std::string host;
    std::uint16_t port = 8095;
    std::string username, token, token_name;
    // The token's id on the account: renewal and log out touch this token
    // alone, even when another device shows the same name. Empty for a
    // sign-in saved before ids were kept; those fall back to the name.
    std::string token_id;
    bool valid() const
    {
        return !host.empty() && !token.empty() && !token_name.empty();
    }
};

// The token the Companion uses and where it came from. A signed-in token wins
// over afterglow.conf's ma_token.
struct Credentials
{
    std::string host, token;
    std::uint16_t port = 8095;
    AccountState::Source source = AccountState::Source::none;
};

namespace account_wire
{
inline constexpr const char *kAuthPath = "/download0/hui/afterglow.auth";
// One token name per console, like the LG app's "Afterglow on <model> (<id>)".
std::string token_name(std::string_view client_id);
std::string encode_auth(const StoredAuth &auth);
StoredAuth decode_auth(std::string_view payload);
StoredAuth load_auth(const std::string &path);
// Checked-container write/remove; "" on success, else the error.
std::string save_auth(const std::string &path, const StoredAuth &auth);
std::string remove_auth(const std::string &path);
// "a.b.c.d" or "a.b.c.d:port"; numeric IPv4 only (the sockets resolve no names).
bool parse_host(std::string_view text, std::uint16_t default_port, std::string &host,
                std::uint16_t &port);

// Checks the sign-in form and fills target (host, port, username, token name).
// host_text falls back to fallback_host when empty. Returns "" or what to fix.
std::string sign_in_target(const std::string &host_text, const std::string &fallback_host,
                           std::uint16_t default_port, const std::string &username,
                           const std::string &password, const std::string &client_id,
                           StoredAuth &target);
Credentials effective_credentials(const StoredAuth &stored, const std::string &config_host,
                                  std::uint16_t config_port, const std::string &config_token);
AccountState account_state(const Credentials &credentials, const StoredAuth &stored);

struct Token
{
    std::string id, name, created_at;
    std::int64_t expires_ms = 0; // 0 when the token never expires or the date is unreadable
    bool long_lived = false;
};
std::vector<Token> decode_tokens(std::string_view result);
// ISO 8601 ("2027-10-14T19:45:14.426601+00:00" or "...Z") to Unix milliseconds; 0 if malformed.
std::int64_t parse_time_ms(std::string_view text);
// Session tokens the login handed out in this name (revoked after sign-in).
std::vector<std::string> session_tokens(const std::vector<Token> &tokens, std::string_view name);
// Long-lived tokens in this name. Listed before a sign-in creates its token,
// these are the ones it supersedes; the new token can never be among them.
std::vector<std::string> long_lived_tokens(const std::vector<Token> &tokens, std::string_view name);
// Every token in this name.
std::vector<std::string> named_tokens(const std::vector<Token> &tokens, std::string_view name);
// The id of the newest long-lived token in this name: the list carries no
// token text, so a token just made is told by being the newest (LG ma.js).
std::string newest_token(const std::vector<Token> &tokens, std::string_view name);
// What log out revokes: this console's own token by id, or every token in its
// name for a sign-in saved before ids were kept.
std::vector<std::string> own_tokens(const std::vector<Token> &tokens, std::string_view name,
                                    std::string_view id);
// What a renewal retires: the old token by id when known, else the long-lived
// tokens in this name made before the fresh one. Never the fresh one.
std::vector<std::string> superseded_tokens(const std::vector<Token> &tokens, std::string_view name,
                                           std::string_view old_id, std::string_view fresh_id);
// True when our token (by id when known, else this name's soonest-expiring
// long-lived one) ends within 30 days. A namesake's expiry is not ours.
bool needs_renewal(const std::vector<Token> &tokens, std::string_view name, std::string_view id,
                   std::int64_t now_ms);

struct Reply
{
    bool ok = false;
    std::string result, error;
    // No answer arrived (closed or timed out): unlike a refusal, the server
    // may still have done what was asked.
    bool lost = false;
};
// One request on an open, greeted API socket; tests substitute a fake server.
using Call = std::function<Reply(const std::string &command, const std::string &args)>;
struct Outcome
{
    bool ok = false, token_changed = false;
    std::string token, token_id, username, message;
    // Tokens to revoke once everything runs on the new token (retire()).
    std::vector<std::string> retire;
};
// The sign-in a new one replaces on the same server: its token id, or
// by_name for one saved before ids were kept (its name's long-lived tokens).
struct Replaces
{
    std::string token_id;
    bool by_name = false;
};
// What a sign-in to target replaces: the stored sign-in, when it is on the same server.
Replaces replaces_for(const StoredAuth &stored, const StoredAuth &target);
Outcome sign_in(const Call &call, const std::string &username, const std::string &password,
                const std::string &token_name, const Replaces &replaces = {});
Outcome log_out(const Call &call, const std::string &token, const std::string &token_name,
                const std::string &token_id);
// Makes a fresh token when ours ends within 30 days. Nothing is revoked here:
// the old token goes in Outcome::retire, revoked by retire() once every open
// connection has been handed the fresh one (LG 1.4.3).
Outcome renew(const Call &call, const std::string &token, const std::string &token_name,
              const std::string &token_id, std::int64_t now_ms);
// Revokes ids, signed in with token (the fresh one).
Outcome retire(const Call &call, const std::string &token, const std::vector<std::string> &ids);
} // namespace account_wire

enum class AccountJob
{
    none,
    sign_in,
    log_out,
    renew,
    retire
};
struct AccountResult
{
    AccountJob job = AccountJob::none;
    bool ok = false, token_changed = false;
    // The stored token after the job (invalid after log out).
    StoredAuth auth;
    std::string message;
    // Superseded tokens: hand auth.token to every connection, then retire() these.
    std::vector<std::string> retire;
    // Why the new token could not be written; "" when it was (or nothing changed).
    std::string save_error;
};
// A finished job's result: writes the new token to path (or removes the file
// on log out). Superseded tokens are retired only once the new one is saved;
// otherwise the next launch would read a token that no longer works.
AccountResult account_result(AccountJob job, const StoredAuth &auth, account_wire::Outcome outcome,
                             const std::string &path);

// Short-lived API sessions on a worker thread; the UI thread only starts jobs
// and collects results. The worker also writes or removes the token file.
class Account
{
  public:
    explicit Account(std::string path = account_wire::kAuthPath);
    ~Account();
    Account(const Account &) = delete;
    Account &operator=(const Account &) = delete;
    // target carries host, port, username and token_name. False while busy.
    bool sign_in(const StoredAuth &target, std::string password,
                 account_wire::Replaces replaces = {});
    bool log_out(const StoredAuth &current);
    bool renew(const StoredAuth &current);
    // Revokes superseded tokens, signed in with current's (fresh) token.
    bool retire(const StoredAuth &current, std::vector<std::string> ids);
    bool busy() const;
    // Collects a finished job once (and joins its thread).
    bool take_result(AccountResult &out);
    void stop();
    void join();

  private:
    bool begin(AccountJob job, const StoredAuth &auth, std::string password);
    account_wire::Replaces replaces_;
    std::vector<std::string> retire_;
    void run();
    std::string path_;
    std::atomic<bool> stopping_{false}, running_{false}, finished_{false};
    pthread_t thread_{};
    bool started_ = false;
    mutable std::mutex mutex_;
    AccountJob job_ = AccountJob::none;
    StoredAuth auth_;
    std::string password_;
    AccountResult result_;
};

// Decides when this PS5's preferences are written to, or restored from, the
// Music Assistant account. Pure: main.cpp feeds it and acts on each Step.
class PreferenceMirror
{
  public:
    // restore: no local preferences file existed at launch.
    explicit PreferenceMirror(bool restore = false) : restore_(restore)
    {
    }
    // A new Companion starts with revision 0; forget the old baseline.
    void reset()
    {
        baseline_ = false;
    }
    struct Step
    {
        std::string save;      // non-empty: Companion::save_account_preferences(save)
        bool restored = false; // preferences holds the account's copy
        Preferences preferences;
    };
    Step update(const Preferences &local, bool connected, std::uint64_t revision,
                const std::string &account_json);
    const std::string &status() const
    {
        return status_;
    }

  private:
    bool restore_ = false, baseline_ = false;
    std::string mirrored_, status_;
};
} // namespace hui::afterglow
