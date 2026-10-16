// Afterglow - Slicing Music Assistant replies: a field, an array's elements, a string.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/json_slice.hpp"
#include "afterglow/protocol.hpp"
#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace hui::afterglow::json_slice
{
namespace
{
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
            if (++depth > 64)
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
} // namespace

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
        if (end < at + 2)
            return {};
        const bool match = s.substr(at + 1, end - at - 2) == key;
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

std::string text(std::string_view s, std::string_view key)
{
    const auto value = field(s, key);
    if (value.empty() || value.size() > 65536 || value.front() != '"')
        return {};
    const std::string object = "{\"v\":" + std::string(value) + "}";
    Json json(object);
    return json.string(json.get(0, "v"));
}

long long integer(std::string_view s, std::string_view key, long long fallback)
{
    const auto value = field(s, key);
    if (value.empty() || value.size() > 24)
        return fallback;
    const std::string digits(value);
    char *end = nullptr;
    errno = 0;
    const long long decoded = std::strtoll(digits.c_str(), &end, 10);
    if (errno || end != digits.c_str() + digits.size())
        return fallback;
    return decoded;
}

std::string quote(std::string_view value)
{
    return json_quote(value);
}
} // namespace hui::afterglow::json_slice
