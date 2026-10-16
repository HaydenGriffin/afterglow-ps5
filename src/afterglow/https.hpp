// Afterglow - One-shot HTTPS GET with verified certificates (BearSSL).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstddef>
#include <functional>
#include <string>

namespace hui::afterglow
{
struct HttpsResult
{
    // A complete HTTP response arrived (any status).
    bool ok = false;
    int status = 0;
    std::string body;
    // Why it failed, for the log: "dns", "connect", "tls 62", "timeout", ...
    std::string error;
};

// GET https://host/path (port 443 unless given), blocking: run it on a worker thread.
// The name is resolved by the system (sceNetResolver on the console), the
// server's certificate chain is verified against the system's CA list
// (/system/common/cert/CA_LIST.cer on the console, the usual bundle files on
// a PC) and the host name is checked. The whole exchange is bounded to
// timeout_ms and the body to max_body; cancelled() is polled every 50 ms.
HttpsResult https_get(const std::string &host, const std::string &path,
                      const std::string &user_agent, std::size_t max_body, int timeout_ms,
                      const std::function<bool()> &cancelled, int port = 443);
// Ends a console name lookup in progress (it cannot poll cancelled()), so
// the app can close promptly. Nothing on a PC.
void https_abort();
} // namespace hui::afterglow
