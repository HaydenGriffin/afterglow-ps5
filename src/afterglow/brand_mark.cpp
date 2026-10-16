// Afterglow - The wordmark on the screens you pass through: setup, connecting and Status.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/brand_mark.hpp"
#include "afterglow/artwork.hpp"
#include "core/save_file.hpp"
#include <memory>
#include <utility>
#include <vector>

namespace hui::afterglow
{
namespace
{
// Wider than this, the mark reads from the full picture; the console draws at
// twice the 1920-pixel canvas, so the half copy still has a texel per pixel.
constexpr float kHalfBelow = 300;

// A 2x2 box filter weighted by alpha, so the clear pixels round the letters
// do not darken their edges.
std::vector<std::uint8_t> halve(const std::vector<std::uint8_t> &rgba, int width, int height,
                                int &out_width, int &out_height)
{
    out_width = width / 2;
    out_height = height / 2;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(out_width) *
                                  static_cast<std::size_t>(out_height) * 4);
    for (int y = 0; y < out_height; ++y)
        for (int x = 0; x < out_width; ++x)
        {
            float colour[3] = {0, 0, 0};
            float alpha = 0;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                {
                    const std::size_t at =
                        (static_cast<std::size_t>(y * 2 + dy) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x * 2 + dx)) *
                        4;
                    const float a = rgba[at + 3];
                    for (int c = 0; c < 3; ++c)
                        colour[c] += static_cast<float>(rgba[at + static_cast<std::size_t>(c)]) * a;
                    alpha += a;
                }
            const std::size_t to =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(out_width) +
                 static_cast<std::size_t>(x)) *
                4;
            for (int c = 0; c < 3; ++c)
                out[to + static_cast<std::size_t>(c)] =
                    alpha > 0 ? static_cast<std::uint8_t>(colour[c] / alpha + 0.5f) : 0;
            out[to + 3] = static_cast<std::uint8_t>(alpha / 4 + 0.5f);
        }
    return out;
}
} // namespace

BrandMark::BrandMark(std::string path, TextureUpload upload)
    : path_(std::move(path)), upload_(std::move(upload))
{
}

void BrandMark::load()
{
    tried_ = true;
    std::string bytes;
    if (!upload_ || !save::read_file(path_, &bytes, 1u << 20))
        return;
    const std::vector<std::uint8_t> body(bytes.begin(), bytes.end());
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
    if (!decode_image(body, rgba, width, height) || width < 2 || height < 2)
        return;
    full_ = upload_(width, height, rgba.data());
    int half_width = 0, half_height = 0;
    const auto half = halve(rgba, width, height, half_width, half_height);
    half_ = upload_(half_width, half_height, half.data());
    width_ = width;
    height_ = height;
}

float BrandMark::height(float width) const
{
    return width_ > 0 ? width * static_cast<float>(height_) / static_cast<float>(width_) : 0;
}

void BrandMark::draw(gfx::DrawList &list, float x, float baseline, float width)
{
    if (!tried_)
        load();
    const std::uint32_t texture = width < kHalfBelow && half_ != 0 ? half_ : full_;
    if (texture == 0)
        return;
    const float h = height(width);
    list.image(texture, {x, baseline - h * kWordmarkBaseline, width, h}, gfx::kFullUv,
               gfx::Color{1, 1, 1, 1});
}

WordmarkDraw wordmark_hook(std::string path, TextureUpload upload)
{
    auto mark = std::make_shared<BrandMark>(std::move(path), std::move(upload));
    return [mark](gfx::DrawList &list, float x, float baseline, float width)
    { mark->draw(list, x, baseline, width); };
}
} // namespace hui::afterglow
