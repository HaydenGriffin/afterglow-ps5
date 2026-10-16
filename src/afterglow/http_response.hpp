// Afterglow - Bounded HTTP/1.1 response framing for the HTTPS client.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace hui::afterglow::http
{
inline constexpr std::size_t kMaxHeader = 16384;
// The most a response may take on the wire: its header, its body and an
// allowance for chunk framing. Real servers add a few bytes per chunk of
// kilobytes; a body sent in tiny chunks would otherwise cost many times its
// size to receive, so it is refused as too large instead.
constexpr std::size_t max_wire(std::size_t max_body)
{
    return max_body > (static_cast<std::size_t>(-1) - kMaxHeader - 4096) / 2
               ? static_cast<std::size_t>(-1)
               : kMaxHeader + max_body + max_body / 8 + 4096;
}

// What the bytes received so far amount to. A response framed by
// Content-Length or chunked transfer coding is complete as soon as its last
// byte arrives, so the reader need not wait for the server to close (TLS
// servers do not always say goodbye cleanly). Without framing the body runs
// to the end of the connection: complete only once closed.
struct Response
{
    enum class State
    {
        incomplete,
        complete,
        malformed,
        too_large
    } state = State::incomplete;
    int status = 0; // 0 until the whole header has arrived
    std::string body;
    // Incomplete with a Content-Length: the wire size that completes it.
    std::size_t complete_at = 0;
};
Response parse_response(std::string_view wire, bool closed, std::size_t max_body);
// The GET request line and headers, ending in the blank line.
std::string get_request(std::string_view host, std::string_view path, std::string_view user_agent);
} // namespace hui::afterglow::http
