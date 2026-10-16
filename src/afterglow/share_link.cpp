// Afterglow - Scan to save: the playing song's public page as a QR code (LG share-link.js).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/share_link.hpp"
#include "afterglow/style.hpp"
#include "third_party/qrcodegen/qrcodegen.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <tuple>

namespace hui::afterglow
{
namespace
{
struct Service
{
    const char *domain, *name, *host;
};
// The order after the service the song played from (LG share-link.js SERVICES).
constexpr Service kServices[] = {
    {"spotify", "Spotify", "spotify.com"},
    {"tidal", "Tidal", "tidal.com"},
    {"apple_music", "Apple Music", "apple.com"},
    {"ytmusic", "YouTube Music", "youtube.com"},
    {"deezer", "Deezer", "deezer.com"},
    {"qobuz", "Qobuz", "qobuz.com"},
    {"soundcloud", "SoundCloud", "soundcloud.com"},
};
constexpr int kServiceCount = static_cast<int>(std::size(kServices));

int service_index(std::string_view domain)
{
    for (int i = 0; i < kServiceCount; ++i)
        if (domain == kServices[i].domain)
            return i;
    return -1;
}
std::string lower(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
// A query parameter whose name carries one of these is taken for a secret.
bool secret_query(std::string_view query)
{
    std::size_t at = 0;
    while (at < query.size())
    {
        auto end = query.find('&', at);
        if (end == std::string_view::npos)
            end = query.size();
        const auto pair = query.substr(at, end - at);
        const auto eq = pair.find('=');
        const auto name = lower(pair.substr(0, eq));
        if (eq != std::string_view::npos)
            for (const char *word : {"token", "key", "auth", "sig", "session"})
                if (name.find(word) != std::string::npos)
                    return true;
        at = end + 1;
    }
    return false;
}
} // namespace

bool public_share_url(std::string_view url, std::string_view domain)
{
    constexpr std::string_view scheme = "https://";
    if (url.size() > kMaxShareUrl || !url.starts_with(scheme) || domain.empty())
        return false;
    const auto rest = url.substr(scheme.size());
    const auto host_end = rest.find_first_of("/?#");
    const auto host = lower(rest.substr(0, host_end));
    if (host.empty() || host.find_first_of("@:") != std::string::npos)
        return false;
    const bool own = host == domain || (host.size() > domain.size() && host.ends_with(domain) &&
                                        host[host.size() - domain.size() - 1] == '.');
    if (!own)
        return false;
    if (host_end == std::string_view::npos)
        return true;
    auto tail = rest.substr(host_end);
    tail = tail.substr(0, tail.find('#'));
    const auto question = tail.find('?');
    return question == std::string_view::npos || !secret_query(tail.substr(question + 1));
}

ShareLink pick_share_link(const std::vector<ProviderLink> &mappings, std::string_view played)
{
    const auto played_domain = played.substr(0, played.find("--"));
    const ProviderLink *best = nullptr;
    std::tuple<int, int, int, std::string, std::string> best_rank;
    for (const auto &m : mappings)
    {
        const int index = service_index(m.domain);
        if (index < 0 || !public_share_url(m.url, kServices[index].host))
            continue;
        auto rank = std::make_tuple(!played.empty() && m.instance == played ? 0 : 1,
                                    !played_domain.empty() && m.domain == played_domain ? 0 : 1,
                                    index, m.instance, m.url);
        if (best == nullptr || rank < best_rank)
        {
            best = &m;
            best_rank = std::move(rank);
        }
    }
    ShareLink out;
    if (best != nullptr)
    {
        out.url = best->url;
        out.service = kServices[service_index(best->domain)].name;
    }
    return out;
}

QrGrid qr_grid(std::string_view text)
{
    QrGrid grid;
    if (text.empty() || text.size() > kMaxShareUrl)
        return grid;
    // Byte mode on a copy (qrcodegen_encodeBinary reads its data from the
    // temporary buffer), so a text with any bytes encodes as the LG app does.
    std::array<std::uint8_t, qrcodegen_BUFFER_LEN_MAX> temp{};
    std::array<std::uint8_t, qrcodegen_BUFFER_LEN_MAX> code{};
    std::copy(text.begin(), text.end(), temp.begin());
    if (!qrcodegen_encodeBinary(temp.data(), text.size(), code.data(), qrcodegen_Ecc_LOW,
                                qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO,
                                true))
        return grid;
    grid.size = qrcodegen_getSize(code.data());
    grid.dark.resize(static_cast<std::size_t>(grid.size * grid.size));
    for (int y = 0; y < grid.size; ++y)
        for (int x = 0; x < grid.size; ++x)
            grid.dark[static_cast<std::size_t>(y * grid.size + x)] =
                qrcodegen_getModule(code.data(), x, y) ? 1 : 0;
    return grid;
}

void draw_share_block(ui::Canvas &canvas, const ShareLink &link, const QrGrid &grid, float right,
                      float top)
{
    if (grid.size <= 0)
        return;
    auto &list = canvas.list;
    const float left = right - kShareCodeSize;
    list.rounded_rect({left, top, kShareCodeSize, kShareCodeSize}, 0, style::kPaper);
    // Whole pixels per module keep every module the same size on the 1080p canvas.
    const int span = grid.size + 2 * kQrQuiet;
    const float module = std::max(1.0f, std::floor(kShareCodeSize / static_cast<float>(span)));
    const float inset = std::floor((kShareCodeSize - module * static_cast<float>(grid.size)) / 2);
    for (int y = 0; y < grid.size; ++y)
        for (int x = 0; x < grid.size; ++x)
        {
            if (!grid.at(x, y))
                continue;
            int run = 1;
            while (grid.at(x + run, y))
                ++run;
            list.rounded_rect({left + inset + module * static_cast<float>(x),
                               top + inset + module * static_cast<float>(y),
                               module * static_cast<float>(run), module},
                              0, style::kInk);
            x += run - 1;
        }
    // The label wraps under the code, as on LG; the service follows it.
    const auto &fonts = canvas.fonts;
    const auto label = ui::paragraph_lines(fonts.semibold, "Scan to open on your phone",
                                           style::kTypeMin, kShareCodeSize, 2);
    const float after = ui::paragraph(list, fonts.semibold, label, left, top + kShareCodeSize + 40,
                                      style::kTypeMin, kShareLineHeight, style::kPaper);
    ui::text(list, fonts.regular, link.service, left, after, style::kTypeMin, style::kMuted);
}
} // namespace hui::afterglow
