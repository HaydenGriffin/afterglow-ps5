// Afterglow - The toast that says which song has come on.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Music card names the song but has to be opened, and never shows a file's own artwork. The
// toast is the kind Lamplight has shown over a game on this firmware. Sony's own music kind does
// not appear over games (librespot-ps5, 2026-10-11).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace afterglow::usbflac
{
inline std::string json_escaped(const std::string &text)
{
    std::string out;
    out.reserve(text.size() + 2);
    for (const char value : text)
    {
        const auto code = static_cast<unsigned char>(value);
        if (value == '"' || value == '\\')
        {
            out += '\\';
            out += value;
        }
        else if (code < 0x20)
        {
            char escape[8];
            std::snprintf(escape, sizeof escape, "\\u%04x", code);
            out += escape;
        }
        else
        {
            out += value;
        }
    }
    return out;
}

// A time as the console wants it: UTC, 2026-11-03T12:34:56.000Z.
inline std::string utc_stamp(std::int64_t unix_ms)
{
    const auto seconds = unix_ms / 1000;
    const auto days = seconds / 86400, rest = seconds % 86400;
    // Days since 1970 to a date (Howard Hinnant's civil_from_days).
    const auto shifted = days + 719468;
    const auto era = shifted / 146097;
    const auto day_of_era = shifted - era * 146097;
    const auto year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const auto day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const auto month_index = (5 * day_of_year + 2) / 153;
    const auto day = day_of_year - (153 * month_index + 2) / 5 + 1;
    const auto month = month_index < 10 ? month_index + 3 : month_index - 9;
    const auto year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
    char out[32];
    std::snprintf(out, sizeof out, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03lldZ",
                  static_cast<long long>(year), static_cast<long long>(month),
                  static_cast<long long>(day), static_cast<long long>(rest / 3600),
                  static_cast<long long>(rest % 3600 / 60), static_cast<long long>(rest % 60),
                  static_cast<long long>(unix_ms % 1000));
    return out;
}

// A later toast takes the place of an earlier one, so quick skips do not pile up.
inline std::string track_toast(const std::string &title, const std::string &artist,
                               const std::string &icon, std::int64_t unix_ms)
{
    const auto body = json_escaped(title);
    std::string view;
    if (!icon.empty())
        view +=
            "\"icon\":{\"type\":\"Url\",\"parameters\":{\"url\":\"" + json_escaped(icon) + "\"}},";
    view += "\"message\":{\"body\":\"" + body + "\"}";
    if (!artist.empty())
        view += ",\"subMessage\":{\"body\":\"" + json_escaped(artist) + "\"}";
    return "{\"rawData\":{\"viewTemplateType\":\"InteractiveToastTemplateB\","
           "\"channelType\":\"Downloads\",\"useCaseId\":\"IDC\",\"toastOverwriteType\":\"Always\","
           "\"isImmediate\":true,\"priority\":100,\"viewData\":{" +
           view +
           "},\"platformViews\":{\"previewDisabled\":{\"viewData\":{\"icon\":{\"type\":"
           "\"Predefined\","
           "\"parameters\":{\"icon\":\"download\"}},\"message\":{\"body\":\"" +
           body + "\"}}}}},\"createdDateTime\":\"" + utc_stamp(unix_ms) +
           "\",\"localNotificationId\":\"" + std::to_string(unix_ms % 1000000000) + "\"}";
}
} // namespace afterglow::usbflac
