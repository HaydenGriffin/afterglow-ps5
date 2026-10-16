// Afterglow - Music Assistant sign-in, token renewal and settings mirroring.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/account.hpp"
#include "afterglow/ma_socket.hpp"
#include "afterglow/protocol.hpp"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <deque>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <utility>

namespace hui::afterglow
{
namespace
{
// Distinct from afterglow.prefs (also Kind::prefs, version 1) so one file can
// never be read as the other.
constexpr std::uint16_t kAuthVersion = 7;
constexpr std::int64_t kRenewWithinMs = 30LL * 86400 * 1000;

// A bounded slicer for the few replies read here (the token list can exceed
// what Json's fixed token table holds); Json decodes the small pieces.
std::size_t value_end(std::string_view s, std::size_t at)
{
    bool quoted = false, escape = false;
    int depth = 0;
    for (std::size_t i = at; i < s.size(); ++i)
    {
        const char c = s[i];
        if (quoted)
        {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
            {
                quoted = false;
                if (!depth)
                    return i + 1;
            }
        }
        else if (c == '"')
            quoted = true;
        else if (c == '[' || c == '{')
        {
            if (++depth > 32)
                return s.size();
        }
        else if (c == ']' || c == '}')
        {
            if (!depth)
                return i;
            if (--depth == 0)
                return i + 1;
        }
        else if (!depth && (c == ',' || std::isspace(static_cast<unsigned char>(c))))
            return i;
    }
    return s.size();
}
void skip_space(std::string_view s, std::size_t &at)
{
    while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at])))
        ++at;
}
std::string_view field(std::string_view s, std::string_view key)
{
    std::size_t at = 0;
    skip_space(s, at);
    if (at == s.size() || s[at++] != '{')
        return {};
    while (at < s.size())
    {
        skip_space(s, at);
        if (at == s.size() || s[at] != '"')
            return {};
        const auto end = value_end(s, at);
        const bool match = end - at >= 2 && s.substr(at + 1, end - at - 2) == key;
        at = end;
        skip_space(s, at);
        if (at == s.size() || s[at++] != ':')
            return {};
        skip_space(s, at);
        const auto finish = value_end(s, at);
        if (match)
            return s.substr(at, finish - at);
        at = finish;
        skip_space(s, at);
        if (at == s.size() || s[at++] != ',')
            return {};
    }
    return {};
}
std::vector<std::string_view> elements(std::string_view s, std::size_t limit)
{
    std::vector<std::string_view> result;
    std::size_t at = 0;
    skip_space(s, at);
    if (at == s.size() || s[at++] != '[')
        return result;
    while (at < s.size() && result.size() < limit)
    {
        skip_space(s, at);
        if (at == s.size() || s[at] == ']')
            break;
        const auto end = value_end(s, at);
        if (end <= at)
            break;
        result.push_back(s.substr(at, end - at));
        at = end;
        skip_space(s, at);
        if (at == s.size() || s[at++] != ',')
            break;
    }
    return result;
}
std::string string_value(std::string_view s)
{
    if (s.empty() || s.size() > 16384 || s.front() != '"')
        return {};
    const std::string object = "{\"v\":" + std::string(s) + "}";
    Json json(object);
    return json.string(json.get(0, "v"));
}
std::string text_field(std::string_view s, std::string_view key)
{
    return string_value(field(s, key));
}
bool clean(std::string_view value)
{
    return value.find_first_of("\r\n") == std::string_view::npos;
}
std::int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::string token_args(std::string_view token)
{
    return "{\"token\":" + json_quote(token) + "}";
}
bool authenticated(const account_wire::Reply &reply)
{
    return reply.ok && field(reply.result, "authenticated") == "true";
}
// How a set of revocations went. A refusal and a lost reply need different
// advice: after a lost reply the token may be gone already.
enum class Revoked
{
    all,
    refused,
    unconfirmed
};
Revoked revoke(const account_wire::Call &call, const std::vector<std::string> &ids)
{
    bool refused = false, lost = false;
    for (const auto &id : ids)
    {
        const auto reply = call("auth/token/revoke", "{\"token_id\":" + json_quote(id) + "}");
        refused |= !reply.ok && !reply.lost;
        lost |= !reply.ok && reply.lost;
    }
    return refused ? Revoked::refused : lost ? Revoked::unconfirmed : Revoked::all;
}
std::vector<account_wire::Token> list_tokens(const account_wire::Call &call)
{
    const auto reply = call("auth/tokens", "{}");
    return reply.ok ? account_wire::decode_tokens(reply.result)
                    : std::vector<account_wire::Token>{};
}

// One greeted API socket: {message_id, command, args} -> {message_id, result | error_code}.
class Session
{
  public:
    explicit Session(std::atomic<bool> &stop) : stopping_(stop), socket_(stop)
    {
        socket_.handler = [this](int opcode, const std::uint8_t *data, std::size_t size)
        {
            if (opcode == 1 && inbox_.size() < 64)
                inbox_.emplace_back(reinterpret_cast<const char *>(data), size);
            return true;
        };
    }
    bool open(const std::string &host, std::uint16_t port)
    {
        if (!socket_.open(host, port, "/ws"))
            return false;
        const auto deadline = sys::monotonic_us() + 10000000;
        while (!stopping_ && sys::monotonic_us() < deadline)
        {
            if (!socket_.tick())
                return false;
            for (const auto &message : inbox_)
                if (!field(message, "server_version").empty())
                {
                    inbox_.clear();
                    return true;
                }
            inbox_.clear();
            poll(nullptr, 0, 10);
        }
        return false;
    }
    account_wire::Reply call(const std::string &command, const std::string &args)
    {
        account_wire::Reply reply;
        const auto id = "c" + std::to_string(++sequence_);
        if (!socket_.send_text("{\"message_id\":" + json_quote(id) +
                               ",\"command\":" + json_quote(command) + ",\"args\":" + args + "}"))
        {
            reply.error = "Connection closed";
            reply.lost = true;
            return reply;
        }
        const auto deadline = sys::monotonic_us() + 15000000;
        while (!stopping_ && sys::monotonic_us() < deadline)
        {
            if (!socket_.tick())
            {
                reply.error = "Connection closed";
                reply.lost = true;
                return reply;
            }
            while (!inbox_.empty())
            {
                const std::string message = std::move(inbox_.front());
                inbox_.pop_front();
                if (text_field(message, "message_id") != id)
                    continue;
                const auto code = field(message, "error_code");
                if (!code.empty() && code != "null")
                {
                    reply.error = text_field(message, "details");
                    if (reply.error.empty())
                        reply.error = "error " + std::string(code);
                    return reply;
                }
                reply.ok = true;
                reply.result = std::string(field(message, "result"));
                return reply;
            }
            poll(nullptr, 0, 10);
        }
        reply.error = "No reply to " + command;
        reply.lost = true;
        return reply;
    }

  private:
    std::atomic<bool> &stopping_;
    ma_socket::Socket socket_;
    std::deque<std::string> inbox_;
    int sequence_ = 0;
};
} // namespace

namespace account_wire
{
std::string token_name(std::string_view client_id)
{
    return "Afterglow on PS5 (" + std::string(client_id) + ")";
}
std::string encode_auth(const StoredAuth &auth)
{
    if (!clean(auth.host) || !clean(auth.username) || !clean(auth.token) ||
        !clean(auth.token_name) || !clean(auth.token_id))
        return {};
    return "host=" + auth.host + "\nport=" + std::to_string(auth.port) +
           "\nusername=" + auth.username + "\ntoken=" + auth.token +
           "\ntoken_name=" + auth.token_name +
           (auth.token_id.empty() ? "" : "\ntoken_id=" + auth.token_id) + "\n";
}
StoredAuth decode_auth(std::string_view text)
{
    StoredAuth auth;
    while (!text.empty())
    {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        const auto eq = line.find('=');
        if (eq == line.npos)
            continue;
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        if (key == "host" && value.size() <= 64)
            auth.host = value;
        else if (key == "username" && value.size() <= 256)
            auth.username = value;
        else if (key == "token" && value.size() <= 4096)
            auth.token = value;
        else if (key == "token_name" && value.size() <= 256)
            auth.token_name = value;
        else if (key == "token_id" && value.size() <= 256)
            auth.token_id = value;
        else if (key == "port")
        {
            unsigned int port = 0;
            char extra = 0;
            const std::string number(value);
            if (std::sscanf(number.c_str(), "%u%c", &port, &extra) == 1 && port > 0 &&
                port <= 65535)
                auth.port = static_cast<std::uint16_t>(port);
        }
    }
    std::string host;
    std::uint16_t port = 0;
    if (!parse_host(auth.host, auth.port, host, port))
        return {};
    return auth;
}
StoredAuth load_auth(const std::string &path)
{
    std::string data;
    if (!save::read_file(path, &data, 16384))
        return {};
    const auto decoded = save::decode(save::Kind::prefs, data);
    if (!decoded.ok || decoded.version != kAuthVersion)
        return {};
    return decode_auth(decoded.payload);
}
std::string save_auth(const std::string &path, const StoredAuth &auth)
{
    const auto payload = encode_auth(auth);
    if (payload.empty())
        return "Token not saved: unexpected characters";
    const auto slash = path.rfind('/');
    if (slash != std::string::npos && slash > 0)
        save::ensure_directory(path.substr(0, slash));
    // The token signs in as this user: readable by the owner only.
    return save::write_atomic(path, save::encode(save::Kind::prefs, kAuthVersion, payload), 0600);
}
std::string remove_auth(const std::string &path)
{
    if (std::remove(path.c_str()) == 0 || errno == ENOENT)
        return {};
    return "Token file could not be removed";
}
bool parse_host(std::string_view text, std::uint16_t default_port, std::string &host,
                std::uint16_t &port)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    if (text.starts_with("http://"))
        text.remove_prefix(7);
    while (text.ends_with('/'))
        text.remove_suffix(1);
    if (text.empty() || text.size() > 64)
        return false;
    port = default_port;
    const auto colon = text.find(':');
    if (colon != text.npos)
    {
        unsigned int value = 0;
        char extra = 0;
        const std::string number(text.substr(colon + 1));
        if (std::sscanf(number.c_str(), "%u%c", &value, &extra) != 1 || value == 0 || value > 65535)
            return false;
        port = static_cast<std::uint16_t>(value);
        text = text.substr(0, colon);
    }
    in_addr address{};
    const std::string candidate(text);
    if (inet_pton(AF_INET, candidate.c_str(), &address) != 1)
        return false;
    host = candidate;
    return port != 0;
}
std::string sign_in_target(const std::string &host_text, const std::string &fallback_host,
                           std::uint16_t default_port, const std::string &username,
                           const std::string &password, const std::string &client_id,
                           StoredAuth &target)
{
    target = {};
    if (!parse_host(host_text.empty() ? fallback_host : host_text, default_port, target.host,
                    target.port))
        return "Enter Music Assistant's IPv4 address, like 192.168.1.10";
    std::string_view name = username;
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
        name.remove_prefix(1);
    while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
        name.remove_suffix(1);
    if (name.empty() || password.empty())
        return "Enter the username and password";
    if (name.size() > 256 || password.size() > 1024 || !clean(name))
        return "Username or password is too long";
    target.username = name;
    target.token_name = token_name(client_id);
    return {};
}
Credentials effective_credentials(const StoredAuth &stored, const std::string &config_host,
                                  std::uint16_t config_port, const std::string &config_token)
{
    Credentials credentials;
    credentials.host = config_host;
    credentials.port = config_port;
    if (stored.valid())
    {
        credentials.host = stored.host;
        credentials.port = stored.port;
        credentials.token = stored.token;
        credentials.source = AccountState::Source::signed_in;
    }
    else if (!config_token.empty() && !config_host.empty())
    {
        credentials.token = config_token;
        credentials.source = AccountState::Source::config;
    }
    return credentials;
}
AccountState account_state(const Credentials &credentials, const StoredAuth &stored)
{
    AccountState state;
    state.source = credentials.source;
    state.host = credentials.host;
    if (credentials.source == AccountState::Source::signed_in)
        state.username = stored.username;
    return state;
}

std::vector<Token> decode_tokens(std::string_view result)
{
    std::vector<Token> tokens;
    for (auto item : elements(result, 512))
    {
        Token token;
        token.id = text_field(item, "token_id");
        if (token.id.empty())
            continue;
        token.name = text_field(item, "name");
        token.created_at = text_field(item, "created_at");
        token.expires_ms = parse_time_ms(text_field(item, "expires_at"));
        token.long_lived = field(item, "is_long_lived") == "true";
        tokens.push_back(std::move(token));
    }
    return tokens;
}
std::int64_t parse_time_ms(std::string_view s)
{
    // YYYY-MM-DDTHH:MM:SS[.fraction](Z|+HH:MM|-HH:MM)
    const auto digits = [&](std::size_t at, std::size_t count, int &out)
    {
        if (at + count > s.size())
            return false;
        out = 0;
        for (std::size_t i = at; i < at + count; ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(s[i])))
                return false;
            out = out * 10 + (s[i] - '0');
        }
        return true;
    };
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (s.size() < 19 || !digits(0, 4, year) || s[4] != '-' || !digits(5, 2, month) ||
        s[7] != '-' || !digits(8, 2, day) || (s[10] != 'T' && s[10] != ' ') ||
        !digits(11, 2, hour) || s[13] != ':' || !digits(14, 2, minute) || s[16] != ':' ||
        !digits(17, 2, second) || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
        minute > 59 || second > 60)
        return 0;
    std::size_t at = 19;
    int millis = 0, scale = 100;
    if (at < s.size() && s[at] == '.')
        for (++at; at < s.size() && std::isdigit(static_cast<unsigned char>(s[at])); ++at)
        {
            millis += (s[at] - '0') * scale;
            scale /= 10;
        }
    int offset_minutes = 0;
    if (at < s.size() && (s[at] == '+' || s[at] == '-'))
    {
        int oh = 0, om = 0;
        if (!digits(at + 1, 2, oh) || at + 3 >= s.size() || s[at + 3] != ':' ||
            !digits(at + 4, 2, om) || at + 6 != s.size())
            return 0;
        offset_minutes = (oh * 60 + om) * (s[at] == '-' ? -1 : 1);
    }
    else if (!(at == s.size() || (s[at] == 'Z' && at + 1 == s.size())))
        return 0;
    // Days from the civil date (Howard Hinnant's algorithm).
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = y / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;
    const std::int64_t seconds =
        days * 86400 + hour * 3600 + minute * 60 + second - offset_minutes * 60;
    return seconds * 1000 + millis;
}
std::vector<std::string> session_tokens(const std::vector<Token> &tokens, std::string_view name)
{
    std::vector<std::string> ids;
    for (const auto &token : tokens)
        if (token.name == name && !token.long_lived)
            ids.push_back(token.id);
    return ids;
}
std::vector<std::string> long_lived_tokens(const std::vector<Token> &tokens, std::string_view name)
{
    std::vector<std::string> ids;
    for (const auto &token : tokens)
        if (token.name == name && token.long_lived)
            ids.push_back(token.id);
    return ids;
}
std::vector<std::string> named_tokens(const std::vector<Token> &tokens, std::string_view name)
{
    std::vector<std::string> ids;
    for (const auto &token : tokens)
        if (token.name == name)
            ids.push_back(token.id);
    return ids;
}
std::string newest_token(const std::vector<Token> &tokens, std::string_view name)
{
    const Token *newest = nullptr;
    for (const auto &token : tokens)
        if (token.name == name && token.long_lived &&
            (newest == nullptr ||
             parse_time_ms(token.created_at) > parse_time_ms(newest->created_at)))
            newest = &token;
    return newest == nullptr ? std::string{} : newest->id;
}
std::vector<std::string> own_tokens(const std::vector<Token> &tokens, std::string_view name,
                                    std::string_view id)
{
    if (id.empty())
        return named_tokens(tokens, name);
    std::vector<std::string> ids;
    for (const auto &token : tokens)
        if (token.id == id)
            ids.push_back(token.id);
    return ids;
}
std::vector<std::string> superseded_tokens(const std::vector<Token> &tokens, std::string_view name,
                                           std::string_view old_id, std::string_view fresh_id)
{
    std::int64_t fresh_at = 0;
    for (const auto &token : tokens)
        if (token.id == fresh_id)
            fresh_at = parse_time_ms(token.created_at);
    std::vector<std::string> ids;
    if (fresh_id.empty())
        return ids; // the fresh token was not found: nothing can be told apart from it
    for (const auto &token : tokens)
    {
        if (token.id == fresh_id)
            continue;
        if (old_id.empty() ? token.name == name && token.long_lived &&
                                 parse_time_ms(token.created_at) < fresh_at
                           : token.id == old_id)
            ids.push_back(token.id);
    }
    return ids;
}
bool needs_renewal(const std::vector<Token> &tokens, std::string_view name, std::string_view id,
                   std::int64_t now)
{
    bool by_id = false;
    for (const auto &token : tokens)
        by_id = by_id || (!id.empty() && token.id == id);
    std::int64_t soonest = 0;
    for (const auto &token : tokens)
        if ((by_id ? token.id == id : token.name == name && token.long_lived) &&
            token.expires_ms > 0 && (soonest == 0 || token.expires_ms < soonest))
            soonest = token.expires_ms;
    return soonest != 0 && soonest - now <= kRenewWithinMs;
}

Replaces replaces_for(const StoredAuth &stored, const StoredAuth &target)
{
    Replaces replaces;
    if (!stored.valid() || stored.host != target.host || stored.port != target.port)
        return replaces;
    replaces.token_id = stored.token_id;
    replaces.by_name = stored.token_id.empty();
    return replaces;
}
Outcome sign_in(const Call &call, const std::string &username, const std::string &password,
                const std::string &token_name, const Replaces &replaces)
{
    Outcome outcome;
    const auto login = call("auth/login", "{\"username\":" + json_quote(username) +
                                              ",\"password\":" + json_quote(password) +
                                              ",\"device_name\":" + json_quote(token_name) + "}");
    const auto session = text_field(login.result, "access_token");
    if (!login.ok || field(login.result, "success") != "true" || session.empty())
    {
        outcome.message = !login.error.empty() ? login.error : text_field(login.result, "error");
        if (outcome.message.empty())
            outcome.message = "Sign-in failed";
        return outcome;
    }
    if (!authenticated(call("auth", token_args(session))))
    {
        outcome.message = "Music Assistant did not accept the new session";
        return outcome;
    }
    // Taken before the new token exists, so retiring these never retires it.
    const auto superseded = replaces.by_name ? long_lived_tokens(list_tokens(call), token_name)
                                             : std::vector<std::string>{};
    const auto created = call("auth/token/create", "{\"name\":" + json_quote(token_name) + "}");
    outcome.token = created.ok ? string_value(created.result) : std::string{};
    if (outcome.token.empty())
    {
        outcome.message = created.error.empty() ? "Music Assistant did not create a token"
                                                : "Token not created: " + created.error;
        return outcome;
    }
    // The login's session token is revoked now, so the account shows one token
    // per console. Failure is harmless: session tokens expire on their own.
    const auto tokens = list_tokens(call);
    outcome.token_id = newest_token(tokens, token_name);
    static_cast<void>(revoke(call, session_tokens(tokens, token_name)));
    // The sign-in this one replaces is retired once the app runs on the new token.
    outcome.retire = superseded;
    if (!replaces.token_id.empty() && replaces.token_id != outcome.token_id)
        outcome.retire.push_back(replaces.token_id);
    outcome.ok = outcome.token_changed = true;
    outcome.username = text_field(field(login.result, "user"), "username");
    if (outcome.username.empty())
        outcome.username = username;
    outcome.message = "Signed in as " + outcome.username;
    return outcome;
}
Outcome log_out(const Call &call, const std::string &token, const std::string &token_name,
                const std::string &token_id)
{
    Outcome outcome;
    // Forgotten here whatever happens; the message says what the account still holds.
    outcome.ok = true;
    const auto session = call("auth", token_args(token));
    if (!authenticated(session))
    {
        outcome.message = std::string("Signed out here. Music Assistant ") +
                          (session.lost ? "did not answer" : "refused the token") +
                          ", so remove \"" + token_name +
                          "\" in Music Assistant's Settings if it is still listed.";
        return outcome;
    }
    const auto reply = call("auth/tokens", "{}");
    if (!reply.ok)
    {
        outcome.message = "Signed out here; the token could not be removed from the account (" +
                          reply.error + "). Remove it in Music Assistant's settings.";
        return outcome;
    }
    const auto own = own_tokens(decode_tokens(reply.result), token_name, token_id);
    // Nothing listed under our id or name: nothing was removed, so say so.
    const auto revoked = own.empty() ? Revoked::unconfirmed : revoke(call, own);
    if (revoked == Revoked::all)
        outcome.message = "Signed out. This PS5's token was removed from the account.";
    else if (revoked == Revoked::refused)
        outcome.message = "Signed out here. Music Assistant refused to remove the token, so "
                          "remove \"" +
                          token_name + "\" in Music Assistant's Settings.";
    else
        outcome.message = "Signed out here. Music Assistant did not confirm the token was "
                          "removed, so remove \"" +
                          token_name + "\" in Music Assistant's Settings if it is still listed.";
    return outcome;
}
Outcome renew(const Call &call, const std::string &token, const std::string &token_name,
              const std::string &token_id, std::int64_t now)
{
    Outcome outcome;
    outcome.token = token;
    outcome.token_id = token_id;
    if (!authenticated(call("auth", token_args(token))))
    {
        outcome.message = "Token check: Music Assistant refused this PS5's token";
        return outcome;
    }
    if (!needs_renewal(list_tokens(call), token_name, token_id, now))
    {
        outcome.ok = true;
        return outcome;
    }
    const auto created = call("auth/token/create", "{\"name\":" + json_quote(token_name) + "}");
    const auto fresh = created.ok ? string_value(created.result) : std::string{};
    if (fresh.empty() || !authenticated(call("auth", token_args(fresh))))
    {
        outcome.message = "Token renewal failed";
        return outcome;
    }
    const auto tokens = list_tokens(call);
    const auto fresh_id = newest_token(tokens, token_name);
    outcome.ok = outcome.token_changed = true;
    outcome.token = fresh;
    outcome.token_id = fresh_id;
    outcome.retire = superseded_tokens(tokens, token_name, token_id, fresh_id);
    outcome.message = "Token renewed";
    return outcome;
}
Outcome retire(const Call &call, const std::string &token, const std::vector<std::string> &ids)
{
    Outcome outcome;
    outcome.token = token;
    if (!authenticated(call("auth", token_args(token))))
    {
        outcome.message = "Old token not removed: Music Assistant refused this PS5's token";
        return outcome;
    }
    for (const auto &id : ids)
        if (!call("auth/token/revoke", "{\"token_id\":" + json_quote(id) + "}").ok)
        {
            outcome.message = "Old token not removed; it expires on its own";
            return outcome;
        }
    outcome.ok = true;
    return outcome;
}
} // namespace account_wire

AccountResult account_result(AccountJob job, const StoredAuth &auth, account_wire::Outcome outcome,
                             const std::string &path)
{
    AccountResult result;
    result.job = job;
    result.auth = auth;
    result.ok = outcome.ok;
    result.token_changed = outcome.token_changed;
    result.message = std::move(outcome.message);
    result.retire = std::move(outcome.retire);
    if (job == AccountJob::log_out)
    {
        result.auth = {};
        const auto error = account_wire::remove_auth(path);
        if (!error.empty())
            result.message += " " + error + ".";
        return result;
    }
    if (!outcome.ok || !outcome.token_changed)
        return result;
    result.auth.token = outcome.token;
    result.auth.token_id = outcome.token_id;
    if (!outcome.username.empty())
        result.auth.username = outcome.username;
    result.save_error = account_wire::save_auth(path, result.auth);
    if (result.save_error.empty())
        return result;
    // The file still holds the previous token, so it stays valid on the account
    // for the next launch; the new one works only until the app closes.
    result.retire.clear();
    if (job == AccountJob::renew)
        result.message = "Token renewed for this session only (" + result.save_error +
                         "); the previous token is kept for the next launch";
    else
        result.message += " (" + result.save_error + "; this sign-in lasts until the app closes)";
    return result;
}

Account::Account(std::string path) : path_(std::move(path))
{
}
Account::~Account()
{
    stop();
    join();
}
bool Account::sign_in(const StoredAuth &target, std::string password,
                      account_wire::Replaces replaces)
{
    if (busy())
        return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        replaces_ = std::move(replaces);
    }
    return begin(AccountJob::sign_in, target, std::move(password));
}
bool Account::log_out(const StoredAuth &current)
{
    return begin(AccountJob::log_out, current, {});
}
bool Account::renew(const StoredAuth &current)
{
    return begin(AccountJob::renew, current, {});
}
bool Account::retire(const StoredAuth &current, std::vector<std::string> ids)
{
    if (busy() || ids.empty())
        return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retire_ = std::move(ids);
    }
    return begin(AccountJob::retire, current, {});
}
bool Account::busy() const
{
    return running_ || finished_;
}
bool Account::begin(AccountJob job, const StoredAuth &auth, std::string password)
{
    if (busy() || stopping_)
        return false;
    join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job_ = job;
        auth_ = auth;
        password_ = std::move(password);
        result_ = {};
    }
    running_ = true;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
    {
        running_ = false;
        return false;
    }
    // The socket reader keeps a 32 KiB buffer on the stack.
    int result = pthread_attr_setstacksize(&attr, 512 * 1024);
    if (!result)
        result = pthread_create(
            &thread_, &attr,
            [](void *self) -> void *
            {
                static_cast<Account *>(self)->run();
                return nullptr;
            },
            this);
    pthread_attr_destroy(&attr);
    started_ = result == 0;
    if (!started_)
        running_ = false;
    return started_;
}
bool Account::take_result(AccountResult &out)
{
    if (!finished_)
        return false;
    join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out = std::move(result_);
        result_ = {};
    }
    finished_ = false;
    return true;
}
void Account::stop()
{
    stopping_ = true;
}
void Account::join()
{
    if (started_)
    {
        pthread_join(thread_, nullptr);
        started_ = false;
    }
}
void Account::run()
{
    AccountJob job;
    StoredAuth auth;
    std::string password;
    account_wire::Replaces replaces;
    std::vector<std::string> retiring;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job = job_;
        auth = auth_;
        password.swap(password_);
        replaces = std::exchange(replaces_, {});
        retiring.swap(retire_);
    }
    Session session(stopping_);
    const bool open = session.open(auth.host, auth.port);
    const account_wire::Call call = [&](const std::string &command, const std::string &args)
    { return session.call(command, args); };
    account_wire::Outcome outcome;
    if (!open && job != AccountJob::log_out)
        outcome.message = "Could not connect to Music Assistant at " + auth.host;
    else if (job == AccountJob::sign_in)
        outcome = account_wire::sign_in(call, auth.username, password, auth.token_name, replaces);
    else if (job == AccountJob::log_out)
    {
        if (open)
            outcome = account_wire::log_out(call, auth.token, auth.token_name, auth.token_id);
        else
        {
            outcome.ok = true;
            outcome.message = "Signed out here; Music Assistant could not be reached, so "
                              "remove \"" +
                              auth.token_name + "\" in its settings.";
        }
    }
    else if (job == AccountJob::renew)
        outcome = account_wire::renew(call, auth.token, auth.token_name, auth.token_id, now_ms());
    else if (job == AccountJob::retire)
        outcome = account_wire::retire(call, auth.token, retiring);
    password.assign(password.size(), '\0');
    AccountResult result = account_result(job, auth, std::move(outcome), path_);
    sys::log("[Afterglow] account job=%d ok=%d", static_cast<int>(job), result.ok ? 1 : 0);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_ = std::move(result);
    }
    finished_ = true;
    running_ = false;
}

PreferenceMirror::Step PreferenceMirror::update(const Preferences &local, bool connected,
                                                std::uint64_t revision,
                                                const std::string &account_json)
{
    Step step;
    if (revision == 0)
    {
        status_ = !local.account_sync ? "Off: settings stay on this PS5"
                  : connected         ? "Reading your account"
                                      : "Waiting for Music Assistant";
        return step;
    }
    // Compared decoded, so the server's own JSON spacing never looks like a change.
    const auto account = account_json.empty()
                             ? std::string{}
                             : Preferences{}.with_synced(account_json).encode_synced();
    if (!baseline_)
    {
        baseline_ = true;
        mirrored_ = account;
        if (restore_ && !account.empty())
        {
            step.restored = true;
            step.preferences = local.with_synced(account_json);
            restore_ = false;
            status_ = "Settings restored from your account";
            mirrored_ = step.preferences.encode_synced();
            return step;
        }
        restore_ = false;
    }
    if (!local.account_sync)
    {
        status_ = "Off: settings stay on this PS5";
        return step;
    }
    const auto current = local.encode_synced();
    if (current != mirrored_)
    {
        step.save = current;
        mirrored_ = current;
    }
    if (mirrored_ == account)
    {
        if (status_ != "Settings restored from your account")
            status_ = "Saved to your Music Assistant account";
    }
    else
        status_ = "Saving to your account";
    return step;
}
} // namespace hui::afterglow
