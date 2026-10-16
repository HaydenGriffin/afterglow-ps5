// Afterglow - Sign-in, token store, renewal and settings mirroring tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/account.hpp"
#include "afterglow/protocol.hpp"
#include <cstdio>
#include <deque>
#include <gtest/gtest.h>
#include <map>
#include <unistd.h>

using namespace hui::afterglow;

namespace
{
// A scripted Music Assistant: each command answers from `replies` and every
// request is recorded, so a flow's exact wire traffic can be checked.
struct FakeServer
{
    std::map<std::string, account_wire::Reply> replies;
    // Answers used once each, in order, before `replies` (a list that changes).
    std::map<std::string, std::deque<account_wire::Reply>> once;
    std::vector<std::pair<std::string, std::string>> requests;
    account_wire::Call call()
    {
        return [this](const std::string &command, const std::string &args)
        {
            requests.emplace_back(command, args);
            auto &queued = once[command];
            if (!queued.empty())
            {
                const auto reply = queued.front();
                queued.pop_front();
                return reply;
            }
            const auto it = replies.find(command);
            return it == replies.end() ? account_wire::Reply{false, {}, "unknown", false}
                                       : it->second;
        };
    }
    void answer_once(const std::string &command, const std::string &result)
    {
        once[command].push_back({true, result, {}, false});
    }
    std::vector<std::string> revoked() const
    {
        std::vector<std::string> ids;
        for (const auto &[command, args] : requests)
            if (command == "auth/token/revoke")
            {
                Json json(args);
                ids.push_back(json.string(json.get(0, "token_id")));
            }
        return ids;
    }
    void answer(const std::string &command, const std::string &result)
    {
        replies[command] = {true, result, {}, false};
    }
};
constexpr const char *kName = "Afterglow on PS5 (afterglow-ps5-232)";
std::string token_json(const char *id, const char *name, bool long_lived, const char *created,
                       const char *expires)
{
    return std::string("{\"token_id\":\"") + id + "\",\"name\":\"" + name + "\",\"created_at\":\"" +
           created + "\",\"expires_at\":" +
           (expires ? std::string("\"") + expires + "\"" : std::string("null")) +
           ",\"is_long_lived\":" + (long_lived ? "true" : "false") + "}";
}
} // namespace

TEST(AfterglowAccount, TokenNameAndStoredAuthRoundTrip)
{
    EXPECT_EQ(account_wire::token_name("afterglow-ps5-232"), kName);
    StoredAuth auth;
    auth.host = "192.0.2.4";
    auth.port = 8095;
    auth.username = "alex";
    auth.token = "abc.def";
    auth.token_name = kName;
    const auto decoded = account_wire::decode_auth(account_wire::encode_auth(auth));
    EXPECT_TRUE(decoded.valid());
    EXPECT_EQ(decoded.host, auth.host);
    EXPECT_EQ(decoded.port, 8095);
    EXPECT_EQ(decoded.username, "alex");
    EXPECT_EQ(decoded.token, "abc.def");
    EXPECT_EQ(decoded.token_name, kName);
    EXPECT_TRUE(decoded.token_id.empty()) << "a sign-in saved before ids were kept";
    auth.token_id = "id-1";
    EXPECT_EQ(account_wire::decode_auth(account_wire::encode_auth(auth)).token_id, "id-1");
    auth.token = "bad\ntoken";
    EXPECT_TRUE(account_wire::encode_auth(auth).empty());
    EXPECT_FALSE(account_wire::decode_auth("host=not-an-ip\ntoken=x\ntoken_name=n\n").valid());
}

TEST(AfterglowAccount, TokenFileUsesCheckedContainer)
{
    char pattern[] = "/tmp/afterglow-auth-XXXXXX";
    const int fd = mkstemp(pattern);
    ASSERT_GE(fd, 0);
    close(fd);
    const std::string path = pattern;
    StoredAuth auth;
    auth.host = "10.0.0.2";
    auth.port = 9000;
    auth.username = "u";
    auth.token = "t";
    auth.token_name = kName;
    EXPECT_EQ(account_wire::save_auth(path, auth), "");
    const auto loaded = account_wire::load_auth(path);
    EXPECT_TRUE(loaded.valid());
    EXPECT_EQ(loaded.port, 9000);
    // A preferences container is not a token file.
    std::FILE *file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    std::fputs("host=10.0.0.2\ntoken=t\ntoken_name=n\n", file);
    std::fclose(file);
    EXPECT_FALSE(account_wire::load_auth(path).valid());
    EXPECT_EQ(account_wire::remove_auth(path), "");
    EXPECT_EQ(account_wire::remove_auth(path), "");
    EXPECT_FALSE(account_wire::load_auth(path).valid());
}

TEST(AfterglowAccount, HostParsingIsNumericIpv4WithOptionalPort)
{
    std::string host;
    std::uint16_t port = 0;
    EXPECT_TRUE(account_wire::parse_host(" 192.0.2.4 ", 8095, host, port));
    EXPECT_EQ(host, "192.0.2.4");
    EXPECT_EQ(port, 8095);
    EXPECT_TRUE(account_wire::parse_host("http://10.1.2.3:9000/", 8095, host, port));
    EXPECT_EQ(host, "10.1.2.3");
    EXPECT_EQ(port, 9000);
    EXPECT_FALSE(account_wire::parse_host("music.local", 8095, host, port));
    EXPECT_FALSE(account_wire::parse_host("10.1.2.3:0", 8095, host, port));
    EXPECT_FALSE(account_wire::parse_host("10.1.2.3:70000", 8095, host, port));
    EXPECT_FALSE(account_wire::parse_host("", 8095, host, port));
}

TEST(AfterglowAccount, SignInFormFallsBackToConfiguredHost)
{
    StoredAuth target;
    EXPECT_EQ(account_wire::sign_in_target("", "192.0.2.4", 8095, " alex ", "pw",
                                           "afterglow-ps5-232", target),
              "");
    EXPECT_EQ(target.host, "192.0.2.4");
    EXPECT_EQ(target.username, "alex");
    EXPECT_EQ(target.token_name, kName);
    EXPECT_FALSE(account_wire::sign_in_target("", "", 8095, "alex", "pw", "id", target).empty());
    EXPECT_FALSE(
        account_wire::sign_in_target("10.0.0.1", "", 8095, "alex", "", "id", target).empty());
}

TEST(AfterglowAccount, SignedInTokenWinsOverConfigToken)
{
    StoredAuth stored;
    auto credentials = account_wire::effective_credentials(stored, "10.0.0.1", 8095, "conf");
    EXPECT_EQ(credentials.source, AccountState::Source::config);
    EXPECT_EQ(credentials.token, "conf");
    EXPECT_EQ(account_wire::effective_credentials(stored, "10.0.0.1", 8095, "").source,
              AccountState::Source::none);
    stored.host = "10.0.0.9";
    stored.port = 9000;
    stored.token = "mine";
    stored.token_name = kName;
    stored.username = "alex";
    credentials = account_wire::effective_credentials(stored, "10.0.0.1", 8095, "conf");
    EXPECT_EQ(credentials.source, AccountState::Source::signed_in);
    EXPECT_EQ(credentials.host, "10.0.0.9");
    EXPECT_EQ(credentials.port, 9000);
    EXPECT_EQ(credentials.token, "mine");
    const auto state = account_wire::account_state(credentials, stored);
    EXPECT_EQ(state.source, AccountState::Source::signed_in);
    EXPECT_EQ(state.username, "alex");
}

TEST(AfterglowAccount, ParsesMusicAssistantTimestamps)
{
    EXPECT_EQ(account_wire::parse_time_ms("1970-01-01T00:00:00Z"), 0);
    EXPECT_EQ(account_wire::parse_time_ms("2027-10-14T19:45:14.426601+00:00"), 1823543114426);
    EXPECT_EQ(account_wire::parse_time_ms("2027-10-14T21:45:14+02:00"), 1823543114000);
    EXPECT_EQ(account_wire::parse_time_ms("2027-10-14"), 0);
    EXPECT_EQ(account_wire::parse_time_ms("2027-13-14T19:45:14Z"), 0);
    EXPECT_EQ(account_wire::parse_time_ms("2027-10-14T19:45:14+0000"), 0);
}

TEST(AfterglowAccount, TokenSelectionsMatchTheLgApp)
{
    const std::string list =
        "[" +
        token_json("old", kName, true, "2026-01-01T00:00:00+00:00", "2026-11-01T00:00:00+00:00") +
        "," +
        token_json("new", kName, true, "2026-10-14T00:00:00+00:00", "2027-10-14T00:00:00+00:00") +
        "," +
        token_json("session", kName, false, "2026-10-15T00:00:00+00:00",
                   "2026-10-16T00:00:00+00:00") +
        "," +
        token_json("tv", "viz-man on Living Room C1", true, "2026-10-10T00:00:00+00:00", nullptr) +
        "]";
    const auto tokens = account_wire::decode_tokens(list);
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[3].expires_ms, 0);
    EXPECT_EQ(account_wire::session_tokens(tokens, kName), std::vector<std::string>{"session"});
    EXPECT_EQ(account_wire::named_tokens(tokens, kName),
              (std::vector<std::string>{"old", "new", "session"}));
    EXPECT_EQ(account_wire::long_lived_tokens(tokens, kName),
              (std::vector<std::string>{"old", "new"}));
    EXPECT_EQ(account_wire::newest_token(tokens, kName), "new");
    EXPECT_EQ(account_wire::newest_token(tokens, "Nobody"), "");
    // Renewal retires the old token by id, else the older ones in the name; never the fresh one.
    EXPECT_EQ(account_wire::superseded_tokens(tokens, kName, "old", "new"),
              std::vector<std::string>{"old"});
    EXPECT_EQ(account_wire::superseded_tokens(tokens, kName, "", "new"),
              std::vector<std::string>{"old"});
    EXPECT_TRUE(account_wire::superseded_tokens(tokens, kName, "new", "new").empty());
    EXPECT_TRUE(account_wire::superseded_tokens(tokens, kName, "", "").empty())
        << "without the fresh token's id nothing can be told apart from it";
    // Log out: our own token by id; every token in the name only without one.
    EXPECT_EQ(account_wire::own_tokens(tokens, kName, "new"), std::vector<std::string>{"new"});
    EXPECT_TRUE(account_wire::own_tokens(tokens, kName, "gone").empty());
    EXPECT_EQ(account_wire::own_tokens(tokens, kName, ""),
              (std::vector<std::string>{"old", "new", "session"}));
    const auto now = account_wire::parse_time_ms("2026-10-15T00:00:00Z");
    // "old" ends 17 days from now: inside the 30-day window.
    EXPECT_TRUE(account_wire::needs_renewal(tokens, kName, "", now));
    EXPECT_TRUE(account_wire::needs_renewal(tokens, kName, "old", now));
    EXPECT_FALSE(account_wire::needs_renewal(tokens, kName, "new", now))
        << "a namesake's expiry is not ours";
    EXPECT_TRUE(account_wire::needs_renewal(tokens, kName, "unlisted", now))
        << "an id the list does not show falls back to the name";
    std::vector<account_wire::Token> fresh(tokens.begin() + 1, tokens.end());
    EXPECT_FALSE(account_wire::needs_renewal(fresh, kName, "", now));
    EXPECT_FALSE(account_wire::needs_renewal(tokens, "viz-man on Living Room C1", "", now));
}

TEST(AfterglowAccount, SignInReplacesOnlyTheStoredSignInOnTheSameServer)
{
    StoredAuth stored;
    stored.host = "10.0.0.2";
    stored.port = 8095;
    stored.token = "t";
    stored.token_name = kName;
    stored.token_id = "mine";
    StoredAuth target = stored;
    target.token.clear();
    auto replaces = account_wire::replaces_for(stored, target);
    EXPECT_EQ(replaces.token_id, "mine");
    EXPECT_FALSE(replaces.by_name);
    stored.token_id.clear();
    replaces = account_wire::replaces_for(stored, target);
    EXPECT_TRUE(replaces.token_id.empty());
    EXPECT_TRUE(replaces.by_name) << "saved before ids were kept: the name stands in";
    target.host = "10.0.0.3";
    replaces = account_wire::replaces_for(stored, target);
    EXPECT_TRUE(replaces.token_id.empty());
    EXPECT_FALSE(replaces.by_name) << "another server's tokens are not this sign-in's";
    EXPECT_FALSE(account_wire::replaces_for(StoredAuth{}, target).by_name);
}

// The account as one console sees it while signing in: its session token,
// the token it is replacing and a namesake of another device. lists_first:
// the sign-in reads the list once before its token exists.
FakeServer signing_in(bool lists_first = false)
{
    FakeServer server;
    server.answer("auth/login", "{\"success\":true,\"access_token\":\"session-token\","
                                "\"user\":{\"username\":\"Alex\"}}");
    server.answer("auth", "{\"authenticated\":true}");
    server.answer("auth/token/create", "\"long-lived\"");
    const std::string before =
        token_json("s1", kName, false, "2026-10-15T00:00:00Z", nullptr) + "," +
        token_json("old", kName, true, "2026-01-15T00:00:01Z", nullptr) + "," +
        token_json("twin", kName, true, "2026-02-15T00:00:01Z", nullptr);
    if (lists_first)
        server.answer_once("auth/tokens", "[" + before + "]");
    server.answer("auth/tokens",
                  "[" + before + "," +
                      token_json("made", kName, true, "2026-10-15T00:00:02Z", nullptr) + "]");
    server.answer("auth/token/revoke", "null");
    return server;
}

TEST(AfterglowAccount, SignInCreatesNamedTokenAndRevokesOnlyTheSessionToken)
{
    FakeServer server = signing_in();
    const auto outcome = account_wire::sign_in(server.call(), "alex", "p\"w", kName);
    EXPECT_TRUE(outcome.ok);
    EXPECT_TRUE(outcome.token_changed);
    EXPECT_EQ(outcome.token, "long-lived");
    EXPECT_EQ(outcome.token_id, "made") << "the newest long-lived token in the name";
    EXPECT_EQ(outcome.username, "Alex");
    ASSERT_GE(server.requests.size(), 4u);
    Json login(server.requests[0].second);
    EXPECT_EQ(server.requests[0].first, "auth/login");
    EXPECT_EQ(login.string(login.get(0, "password")), "p\"w");
    EXPECT_EQ(login.string(login.get(0, "device_name")), kName);
    Json auth(server.requests[1].second);
    EXPECT_EQ(auth.string(auth.get(0, "token")), "session-token");
    EXPECT_EQ(server.requests[2].first, "auth/token/create");
    Json create(server.requests[2].second);
    EXPECT_EQ(create.string(create.get(0, "name")), kName);
    // A first sign-in leaves every namesake alone: another device may share the name.
    EXPECT_EQ(server.revoked(), std::vector<std::string>{"s1"});
    EXPECT_TRUE(outcome.retire.empty());
}

TEST(AfterglowAccount, SignInAgainRetiresTheReplacedTokenAfterTheHandOver)
{
    FakeServer server = signing_in();
    account_wire::Replaces replaces;
    replaces.token_id = "old";
    auto outcome = account_wire::sign_in(server.call(), "alex", "pw", kName, replaces);
    EXPECT_TRUE(outcome.ok);
    EXPECT_EQ(server.revoked(), std::vector<std::string>{"s1"}) << "nothing else revoked yet";
    EXPECT_EQ(outcome.retire, std::vector<std::string>{"old"});

    // A sign-in saved before ids were kept: its name's long-lived tokens,
    // listed before the new one existed, so never the new one.
    FakeServer legacy = signing_in(true);
    replaces = {};
    replaces.by_name = true;
    outcome = account_wire::sign_in(legacy.call(), "alex", "pw", kName, replaces);
    EXPECT_EQ(legacy.requests[2].first, "auth/tokens");
    EXPECT_EQ(legacy.requests[3].first, "auth/token/create");
    EXPECT_EQ(outcome.retire, (std::vector<std::string>{"old", "twin"}));
    EXPECT_EQ(outcome.token_id, "made");
}

TEST(AfterglowAccount, SignInReportsLoginErrors)
{
    FakeServer server;
    server.answer("auth/login", "{\"success\":false,\"error\":\"Invalid username or password\"}");
    auto outcome = account_wire::sign_in(server.call(), "alex", "bad", kName);
    EXPECT_FALSE(outcome.ok);
    EXPECT_EQ(outcome.message, "Invalid username or password");
    EXPECT_EQ(server.requests.size(), 1u);
    server.replies["auth/login"] = {false, {}, "Authentication failed"};
    outcome = account_wire::sign_in(server.call(), "alex", "bad", kName);
    EXPECT_EQ(outcome.message, "Authentication failed");
    server.answer("auth/login", "{\"success\":true,\"access_token\":\"s\"}");
    server.answer("auth", "{\"authenticated\":true}");
    server.answer("auth/token/create", "null");
    outcome = account_wire::sign_in(server.call(), "alex", "pw", kName);
    EXPECT_FALSE(outcome.ok);
    EXPECT_TRUE(outcome.token.empty());
}

TEST(AfterglowAccount, LogOutRevokesOnlyThisConsolesOwnToken)
{
    FakeServer server;
    server.answer("auth", "{\"authenticated\":true}");
    server.answer("auth/tokens",
                  "[" + token_json("a", kName, true, "2026-01-01T00:00:00Z", nullptr) + "," +
                      token_json("b", "Other", true, "2026-01-01T00:00:00Z", nullptr) + "," +
                      token_json("c", kName, false, "2026-01-01T00:00:00Z", nullptr) + "," +
                      token_json("twin", kName, true, "2026-01-02T00:00:00Z", nullptr) + "]");
    server.answer("auth/token/revoke", "null");
    auto outcome = account_wire::log_out(server.call(), "mine", kName, "a");
    EXPECT_TRUE(outcome.ok);
    EXPECT_EQ(server.revoked(), std::vector<std::string>{"a"}) << "the namesake survives";
    // A sign-in saved before ids were kept removes every token in its name.
    FakeServer legacy;
    legacy.replies = server.replies;
    outcome = account_wire::log_out(legacy.call(), "mine", kName, "");
    EXPECT_EQ(legacy.revoked(), (std::vector<std::string>{"a", "c", "twin"}));
    FakeServer refused;
    refused.answer("auth", "{\"authenticated\":false}");
    outcome = account_wire::log_out(refused.call(), "mine", kName, "a");
    EXPECT_TRUE(outcome.ok) << "forgotten locally even when the server refuses";
    EXPECT_NE(outcome.message.find(kName), std::string::npos);
    EXPECT_TRUE(refused.revoked().empty());
}

TEST(AfterglowAccount, LogOutSaysWhenTheAccountDidNotRemoveTheToken)
{
    const auto listed =
        "[" + token_json("a", kName, true, "2026-01-01T00:00:00Z", nullptr) + "]";
    const auto removed = [](const account_wire::Outcome &outcome)
    { return outcome.message.find("was removed from the account") != std::string::npos; };

    FakeServer done;
    done.answer("auth", "{\"authenticated\":true}");
    done.answer("auth/tokens", listed);
    done.answer("auth/token/revoke", "null");
    auto outcome = account_wire::log_out(done.call(), "mine", kName, "a");
    EXPECT_TRUE(outcome.ok);
    EXPECT_TRUE(removed(outcome)) << outcome.message;

    FakeServer refusing;
    refusing.replies = done.replies;
    refusing.replies["auth/token/revoke"] = {false, {}, "Not allowed", false};
    outcome = account_wire::log_out(refusing.call(), "mine", kName, "a");
    EXPECT_TRUE(outcome.ok) << "forgotten locally all the same";
    EXPECT_FALSE(removed(outcome)) << outcome.message;
    EXPECT_NE(outcome.message.find("refused"), std::string::npos) << outcome.message;
    EXPECT_NE(outcome.message.find(kName), std::string::npos) << outcome.message;
    EXPECT_NE(outcome.message.find("Settings"), std::string::npos) << outcome.message;

    FakeServer silent;
    silent.replies = done.replies;
    silent.replies["auth/token/revoke"] = {false, {}, "No reply to auth/token/revoke", true};
    outcome = account_wire::log_out(silent.call(), "mine", kName, "a");
    EXPECT_TRUE(outcome.ok);
    EXPECT_FALSE(removed(outcome)) << outcome.message;
    EXPECT_NE(outcome.message.find("did not confirm"), std::string::npos) << outcome.message;
    EXPECT_NE(outcome.message.find(kName), std::string::npos) << outcome.message;

    // Our token is not in the list at all: nothing was removed.
    FakeServer missing;
    missing.replies = done.replies;
    missing.answer("auth/tokens", "[]");
    outcome = account_wire::log_out(missing.call(), "mine", kName, "a");
    EXPECT_FALSE(removed(outcome)) << outcome.message;
    EXPECT_TRUE(missing.revoked().empty());

    FakeServer unanswered;
    unanswered.replies["auth"] = {false, {}, "No reply to auth", true};
    outcome = account_wire::log_out(unanswered.call(), "mine", kName, "a");
    EXPECT_TRUE(outcome.ok);
    EXPECT_NE(outcome.message.find("did not answer"), std::string::npos) << outcome.message;
}

TEST(AfterglowAccount, RenewalRetiresTheOldTokenOnlyOnceTheNewOneIsSaved)
{
    char pattern[] = "/tmp/afterglow-auth-XXXXXX";
    const int fd = mkstemp(pattern);
    ASSERT_GE(fd, 0);
    close(fd);
    StoredAuth current;
    current.host = "192.0.2.4";
    current.username = "alex";
    current.token = "old-token";
    current.token_id = "old";
    current.token_name = kName;
    account_wire::Outcome renewed;
    renewed.ok = renewed.token_changed = true;
    renewed.token = "fresh-token";
    renewed.token_id = "fresh";
    renewed.retire = {"old"};
    renewed.message = "Token renewed";

    // A file in place of the folder: the token file cannot be written.
    const std::string unwritable = std::string(pattern) + "/afterglow.auth";
    auto result = account_result(AccountJob::renew, current, renewed, unwritable);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.auth.token, "fresh-token") << "the fresh token still serves this session";
    EXPECT_FALSE(result.save_error.empty());
    EXPECT_TRUE(result.retire.empty()) << "the old token is what the next launch reads";
    EXPECT_NE(result.message.find("previous token is kept"), std::string::npos) << result.message;

    // The same for a sign-in that replaces an earlier one.
    auto signed_in = renewed;
    signed_in.message = "Signed in as alex";
    result = account_result(AccountJob::sign_in, current, signed_in, unwritable);
    EXPECT_TRUE(result.retire.empty());
    EXPECT_NE(result.message.find("lasts until the app closes"), std::string::npos);

    result = account_result(AccountJob::renew, current, renewed, pattern);
    EXPECT_TRUE(result.save_error.empty());
    EXPECT_EQ(result.retire, std::vector<std::string>{"old"});
    EXPECT_EQ(account_wire::load_auth(pattern).token, "fresh-token");
    std::remove(pattern);
}

TEST(AfterglowAccount, RenewalOnlyWithinThirtyDaysAndRetiresTheOldTokenAfterTheHandOver)
{
    const auto now = account_wire::parse_time_ms("2026-10-15T00:00:00Z");
    FakeServer later;
    later.answer("auth", "{\"authenticated\":true}");
    later.answer(
        "auth/tokens",
        "[" + token_json("a", kName, true, "2026-01-01T00:00:00Z", "2027-01-01T00:00:00Z") + "," +
            token_json("twin", kName, true, "2026-01-01T00:00:00Z", "2026-10-20T00:00:00Z") + "]");
    auto outcome = account_wire::renew(later.call(), "mine", kName, "a", now);
    EXPECT_TRUE(outcome.ok);
    EXPECT_FALSE(outcome.token_changed) << "a namesake near expiry is not ours to renew";
    EXPECT_EQ(outcome.token, "mine");
    EXPECT_EQ(outcome.token_id, "a");
    for (const auto &request : later.requests)
        EXPECT_NE(request.first, "auth/token/create");

    FakeServer soon;
    soon.answer("auth", "{\"authenticated\":true}");
    const std::string before =
        token_json("old", kName, true, "2026-01-01T00:00:00Z", "2026-11-01T00:00:00Z") + "," +
        token_json("twin", kName, true, "2026-02-01T00:00:00Z", "2027-02-01T00:00:00Z");
    soon.answer_once("auth/tokens", "[" + before + "]");
    soon.answer("auth/tokens", "[" + before + "," +
                                   token_json("fresh", kName, true, "2026-10-15T00:00:00Z",
                                              "2027-10-15T00:00:00Z") +
                                   "]");
    soon.answer("auth/token/create", "\"fresh-token\"");
    soon.answer("auth/token/revoke", "null");
    outcome = account_wire::renew(soon.call(), "mine", kName, "old", now);
    EXPECT_TRUE(outcome.ok);
    EXPECT_TRUE(outcome.token_changed);
    EXPECT_EQ(outcome.token, "fresh-token");
    EXPECT_EQ(outcome.token_id, "fresh");
    EXPECT_TRUE(soon.revoked().empty()) << "nothing is revoked before the hand-over";
    EXPECT_EQ(outcome.retire, std::vector<std::string>{"old"}) << "by id: the twin survives";
    // After the hand-over: signed in with the fresh token, the old one goes.
    soon.requests.clear();
    const auto retired = account_wire::retire(soon.call(), "fresh-token", outcome.retire);
    EXPECT_TRUE(retired.ok);
    Json auth(soon.requests[0].second);
    EXPECT_EQ(auth.string(auth.get(0, "token")), "fresh-token");
    EXPECT_EQ(soon.revoked(), std::vector<std::string>{"old"});
    FakeServer refused;
    refused.answer("auth", "{\"authenticated\":false}");
    EXPECT_FALSE(account_wire::retire(refused.call(), "fresh-token", {"old"}).ok);
    EXPECT_TRUE(refused.revoked().empty());
}

TEST(AfterglowAccount, AccountWorkerReportsUnreachableServer)
{
    char pattern[] = "/tmp/afterglow-auth-XXXXXX";
    const int fd = mkstemp(pattern);
    ASSERT_GE(fd, 0);
    close(fd);
    Account account(pattern);
    StoredAuth target;
    target.host = "127.0.0.1";
    target.port = 1; // nothing listens: the connect is refused at once
    target.username = "u";
    target.token_name = kName;
    ASSERT_TRUE(account.sign_in(target, "pw"));
    EXPECT_FALSE(account.sign_in(target, "pw")) << "one job at a time";
    AccountResult result;
    for (int i = 0; i < 500 && !account.take_result(result); ++i)
        usleep(10000);
    EXPECT_EQ(result.job, AccountJob::sign_in);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(account.busy());
    EXPECT_NE(result.message.find("127.0.0.1"), std::string::npos);
    std::remove(pattern);
}

TEST(AfterglowAccount, PreferencesSyncCodecRoundTripsTheSyncedSubset)
{
    Preferences local;
    local.brightness = 40;
    local.tint = true;
    local.lyrics_offset_ms = -250;
    local.scan_cursor = 77;
    local.player = "speaker";
    local.favorites = {"A"};
    local.account_sync = false;
    const auto json = local.encode_synced();
    EXPECT_NE(json.find("\"brightness\":40"), std::string::npos);
    EXPECT_NE(json.find("\"lyrics_offset_ms\":-250"), std::string::npos);
    EXPECT_EQ(json.find("scan_cursor"), std::string::npos);
    EXPECT_EQ(json.find("player"), std::string::npos);
    EXPECT_EQ(json.find("\"favorite\""), std::string::npos);
    EXPECT_EQ(json.find("favorite_pending"), std::string::npos);
    EXPECT_EQ(json.find("quality"), std::string::npos);
    EXPECT_EQ(json.find("account_sync"), std::string::npos);
    Preferences other;
    other.scan_cursor = 5;
    other.favorites = {"B"};
    const auto restored = other.with_synced(json);
    EXPECT_EQ(restored.brightness, 40);
    EXPECT_TRUE(restored.tint);
    EXPECT_EQ(restored.lyrics_offset_ms, -250);
    EXPECT_EQ(restored.scan_cursor, 5) << "local-only fields stay";
    EXPECT_EQ(restored.favorites, std::vector<std::string>{"B"});
    EXPECT_TRUE(restored.account_sync);
    EXPECT_EQ(restored.encode_synced(), json);
}

TEST(AfterglowAccount, PreferencesSyncDecodeIsBoundedAndClamped)
{
    Preferences base;
    auto p = base.with_synced(
        "{ \"brightness\" : 5, \"tint\": true, \"fps\": 999, \"scan_cursor\": 9, \"x\": 1 }");
    EXPECT_EQ(p.brightness, 10);
    EXPECT_TRUE(p.tint);
    EXPECT_EQ(p.fps, 60);
    EXPECT_EQ(p.scan_cursor, 0);
    // Malformed input keeps what was read before the fault.
    p = base.with_synced("{\"brightness\":50,\"tint\":\"yes\",\"karaoke\":0}");
    EXPECT_EQ(p.brightness, 50);
    EXPECT_TRUE(p.karaoke);
    EXPECT_EQ(base.with_synced("not json").encode(), base.encode());
    EXPECT_EQ(base.with_synced("{\"brightness\":99999999999999}").brightness, 100);
    EXPECT_EQ(base.with_synced(std::string(20000, '{')).encode(), base.encode());
}

TEST(AfterglowAccount, MirrorRestoresOnFreshInstallThenSavesChanges)
{
    PreferenceMirror mirror(true);
    Preferences local;
    auto step = mirror.update(local, false, 0, "");
    EXPECT_TRUE(step.save.empty());
    EXPECT_EQ(mirror.status(), "Waiting for Music Assistant");
    Preferences remote;
    remote.brightness = 30;
    step = mirror.update(local, true, 1, remote.encode_synced());
    ASSERT_TRUE(step.restored);
    EXPECT_EQ(step.preferences.brightness, 30);
    EXPECT_TRUE(step.save.empty()) << "a restore never writes the defaults back";
    local = step.preferences;
    step = mirror.update(local, true, 1, remote.encode_synced());
    EXPECT_FALSE(step.restored);
    EXPECT_TRUE(step.save.empty());
    local.brightness = 60;
    step = mirror.update(local, true, 1, remote.encode_synced());
    EXPECT_EQ(step.save, local.encode_synced());
    EXPECT_EQ(mirror.status(), "Saving to your account");
    step = mirror.update(local, true, 2, step.save);
    EXPECT_TRUE(step.save.empty());
    EXPECT_EQ(mirror.status(), "Saved to your Music Assistant account");
}

TEST(AfterglowAccount, MirrorWithLocalPreferencesPushesThemAndHonoursSyncOff)
{
    PreferenceMirror mirror(false);
    Preferences local;
    local.brightness = 70;
    // The account's copy is compared decoded: spacing differences are not changes.
    auto step = mirror.update(local, true, 1, "{ \"brightness\": 70 }");
    EXPECT_FALSE(step.restored);
    EXPECT_TRUE(step.save.empty());
    PreferenceMirror kept(false);
    step = kept.update(local, true, 1, "{\"brightness\":50}");
    EXPECT_FALSE(step.restored);
    EXPECT_EQ(step.save, local.encode_synced()) << "a PS5 with its own file wins";
    PreferenceMirror empty(true);
    step = empty.update(local, true, 1, "");
    EXPECT_FALSE(step.restored) << "nothing on the account to restore";
    EXPECT_EQ(step.save, local.encode_synced());
    local.account_sync = false;
    local.brightness = 20;
    step = mirror.update(local, true, 1, "{}");
    EXPECT_TRUE(step.save.empty());
    EXPECT_EQ(mirror.status(), "Off: settings stay on this PS5");
    mirror.reset();
    local.account_sync = true;
    step = mirror.update(local, true, 1, local.encode_synced());
    EXPECT_TRUE(step.save.empty()) << "a new baseline after a Companion restart";
}
