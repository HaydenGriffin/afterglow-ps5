// Afterglow - Bounded HTTP/1.1 response framing for the HTTPS client.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/http_response.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>

namespace hui::afterglow::http
{
namespace
{
using State = Response::State;

bool number(std::string_view text, std::size_t &value, int base)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
// The value of a header (names compared without case), or npos in found.
std::string_view header(std::string_view head, std::string_view name, bool &found)
{
    found = false;
    for (std::size_t at = head.find("\r\n"); at != std::string_view::npos;)
    {
        const auto start = at + 2;
        const auto end = head.find("\r\n", start);
        const auto line = head.substr(start, end == std::string_view::npos ? end : end - start);
        at = end;
        if (line.size() <= name.size() || line[name.size()] != ':')
            continue;
        if (!std::equal(name.begin(), name.end(), line.begin(),
                        [](char a, char b)
                        {
                            return std::tolower(static_cast<unsigned char>(a)) ==
                                   std::tolower(static_cast<unsigned char>(b));
                        }))
            continue;
        found = true;
        return line.substr(name.size() + 1);
    }
    return {};
}
bool chunked(std::string_view value)
{
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered.find("chunked") != std::string::npos;
}
State dechunk(std::string_view payload, std::size_t max_body, std::string &body)
{
    for (;;)
    {
        const auto end = payload.find("\r\n");
        if (end == std::string_view::npos)
            return payload.size() > 64 ? State::malformed : State::incomplete;
        // Extensions are looked for on the size line alone: searching the rest
        // of the payload for every chunk would make many chunks quadratic.
        const auto line = payload.substr(0, end);
        std::size_t count = 0;
        if (!number(line.substr(0, line.find(';')), count, 16))
            return State::malformed;
        payload.remove_prefix(end + 2);
        if (count == 0)
            // Trailers are not needed; the zero chunk ends the body.
            return State::complete;
        if (count > max_body - std::min(max_body, body.size()))
            return State::too_large;
        if (payload.size() < count + 2)
            return State::incomplete;
        if (payload.substr(count, 2) != "\r\n")
            return State::malformed;
        body.append(payload.substr(0, count));
        payload.remove_prefix(count + 2);
    }
}
} // namespace

namespace
{
Response parse_framed(std::string_view wire, bool closed, std::size_t max_body)
{
    Response out;
    const auto boundary = wire.find("\r\n\r\n");
    if (boundary == std::string_view::npos)
    {
        out.state = wire.size() > kMaxHeader ? State::malformed
                    : closed                 ? State::malformed
                                             : State::incomplete;
        return out;
    }
    if (boundary > kMaxHeader || !(wire.starts_with("HTTP/1.1 ") || wire.starts_with("HTTP/1.0 ")))
    {
        out.state = State::malformed;
        return out;
    }
    std::size_t status = 0;
    if (wire.size() < 12 || !number(wire.substr(9, 3), status, 10) || status < 100 || status > 599)
    {
        out.state = State::malformed;
        return out;
    }
    out.status = static_cast<int>(status);
    const auto head = wire.substr(0, boundary);
    const auto payload = wire.substr(boundary + 4);
    bool found = false;
    const auto coding = header(head, "Transfer-Encoding", found);
    if (found && chunked(coding))
    {
        // Checked even when the last chunk is in: the framing itself is the cost.
        if (wire.size() > max_wire(max_body))
        {
            out.state = State::too_large;
            return out;
        }
        out.state = dechunk(payload, max_body, out.body);
        if (out.state == State::incomplete && closed)
            out.state = State::malformed;
        if (out.state != State::complete)
            out.body.clear();
        return out;
    }
    const auto length_text = header(head, "Content-Length", found);
    std::size_t length = 0;
    if (found)
    {
        if (!number(length_text, length, 10))
            out.state = State::malformed;
        else if (length > max_body)
            out.state = State::too_large;
        else if (payload.size() >= length)
        {
            out.state = State::complete;
            out.body.assign(payload.substr(0, length));
        }
        else
        {
            out.state = closed ? State::malformed : State::incomplete;
            out.complete_at = boundary + 4 + length;
        }
        return out;
    }
    if (payload.size() > max_body)
        out.state = State::too_large;
    else if (closed)
    {
        out.state = State::complete;
        out.body.assign(payload);
    }
    return out;
}
} // namespace

Response parse_response(std::string_view wire, bool closed, std::size_t max_body)
{
    auto out = parse_framed(wire, closed, max_body);
    if (out.state != State::complete && wire.size() > max_wire(max_body))
    {
        out.state = State::too_large;
        out.body.clear();
    }
    return out;
}

std::string get_request(std::string_view host, std::string_view path, std::string_view user_agent)
{
    std::string request = "GET ";
    request += path;
    request += " HTTP/1.1\r\nHost: ";
    request += host;
    request += "\r\nUser-Agent: ";
    request += user_agent;
    request += "\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nConnection: "
               "close\r\n\r\n";
    return request;
}
} // namespace hui::afterglow::http
