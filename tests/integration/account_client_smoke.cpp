// Afterglow - Host smoke test for console sign-in, token renewal and log-out.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/account.hpp"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

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

namespace
{
using namespace hui::afterglow;
bool wait(Account &account, AccountResult &result)
{
    for (int i = 0; i < 1000; ++i)
    {
        if (account.take_result(result))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

// argv: port, token file path. Prints one line per step; exit code 0 = all passed.
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    const auto port = static_cast<std::uint16_t>(std::atoi(argv[1]));
    const std::string path = argv[2];
    Account account(path);
    AccountResult result;

    StoredAuth target;
    if (!account_wire::sign_in_target("127.0.0.1:" + std::to_string(port), "", 8095, "fixture",
                                      "wrong", "afterglow-ps5-test", target)
             .empty() ||
        !account.sign_in(target, "wrong") || !wait(account, result) || result.ok)
        return 3;
    std::printf("rejected=1 message=%s\n", result.message.c_str());
    if (account_wire::load_auth(path).valid())
        return 4;

    if (!account.sign_in(target, "secret") || !wait(account, result) || !result.ok)
        return 5;
    const auto stored = account_wire::load_auth(path);
    std::printf("signed_in=1 user=%s token=%s id=%s\n", stored.username.c_str(),
                stored.token.c_str(), stored.token_id.c_str());
    if (stored.token != "long-1" || stored.token_id != "long-1" ||
        stored.username != "Fixture User" ||
        stored.token_name != "Afterglow on PS5 (afterglow-ps5-test)" || !result.retire.empty())
        return 6;

    // Signing in again replaces long-1, retired once main.cpp runs on long-2.
    if (!account.sign_in(target, "secret", account_wire::replaces_for(stored, target)) ||
        !wait(account, result) || !result.ok || result.retire != std::vector<std::string>{"long-1"})
        return 11;
    const auto again = account_wire::load_auth(path);
    std::printf("signed_in_again=1 token=%s\n", again.token.c_str());
    if (again.token != "long-2" || again.token_id != "long-2")
        return 12;
    if (!account.retire(again, result.retire) || !wait(account, result) || !result.ok)
        return 13;

    if (!account.renew(again) || !wait(account, result) || !result.ok || !result.token_changed ||
        result.retire != std::vector<std::string>{"long-2"})
        return 7;
    const auto renewed = account_wire::load_auth(path);
    std::printf("renewed=1 token=%s\n", renewed.token.c_str());
    if (renewed.token != "long-3" || renewed.token_id != "long-3")
        return 8;
    if (!account.retire(renewed, result.retire) || !wait(account, result) || !result.ok)
        return 14;

    if (!account.log_out(renewed) || !wait(account, result) || !result.ok)
        return 9;
    std::printf("logged_out=1 file=%d message=%s\n", account_wire::load_auth(path).valid() ? 1 : 0,
                result.message.c_str());
    if (result.message.find("was removed from the account") == std::string::npos)
        return 15;
    return account_wire::load_auth(path).valid() ? 10 : 0;
}
