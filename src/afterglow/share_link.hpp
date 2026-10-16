// Afterglow - Scan to save: the playing song's public page as a QR code (LG share-link.js).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "ui/components/component.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hui::afterglow
{
// One of Music Assistant's provider mappings (2.10 ProviderMapping) for the song.
struct ProviderLink
{
    std::string domain, instance, url;
};
// What Details offers: the url and the service it belongs to ("Spotify"), or empty.
struct ShareLink
{
    std::string url, service;
};
inline constexpr std::size_t kMaxShareUrl = 300;

// Whether url is the service's own public https page (domain "spotify.com"):
// no user info, no port, the host that domain or below it, no token-like
// query parameter, at most kMaxShareUrl characters.
bool public_share_url(std::string_view url, std::string_view domain);
// The link to show: the mapping the song played from (played is
// streamdetails.provider), then the same service, then a fixed service order,
// then instance and url. Empty when no mapping is a public service's page.
ShareLink pick_share_link(const std::vector<ProviderLink> &mappings, std::string_view played);

// A QR code's modules: byte mode, error correction Low raised while the
// version stays the same, automatic mask (Nayuki's qrcodegen, as on LG).
// size 0 when the text does not fit.
struct QrGrid
{
    int size = 0;
    std::vector<std::uint8_t> dark; // row-major, size * size
    bool at(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < size && y < size &&
               dark[static_cast<std::size_t>(y * size + x)] != 0;
    }
};
QrGrid qr_grid(std::string_view text);

// Modules of paper round the code.
inline constexpr int kQrQuiet = 4;
// The Details block: the code's paper square, then two lines of label type.
inline constexpr float kShareCodeSize = 280;
inline constexpr float kShareLineHeight = 34;
inline constexpr float kShareBlockHeight = kShareCodeSize + 40 + 3 * kShareLineHeight;
// Draws the code (flat ink squares on a paper square, whole pixels per
// module, centred in it), "Scan to open on your phone" and the service, with
// the block's right edge at right and its top at top.
void draw_share_block(ui::Canvas &canvas, const ShareLink &link, const QrGrid &grid, float right,
                      float top);
} // namespace hui::afterglow
