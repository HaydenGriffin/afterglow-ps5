// Afterglow - Scan to save: which public page of the song to show (LG share-link.js).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Apart from the QR code and its drawing (share_link.cpp), so the network
// worker that picks the link links without the renderer.
#include "afterglow/share_link.hpp"
#include <cctype>
#include <iterator>
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
} // namespace hui::afterglow
