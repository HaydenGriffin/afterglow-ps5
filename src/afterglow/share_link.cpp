// Afterglow - Scan to save: the playing song's public page as a QR code (LG share-link.js).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/share_link.hpp"
#include "afterglow/style.hpp"
#include "third_party/qrcodegen/qrcodegen.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace hui::afterglow
{
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
