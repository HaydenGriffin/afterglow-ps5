// Afterglow - The generated cloud texture MilkDrop presets get for images that are not shipped.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <vector>

namespace hui::afterglow
{
// Some presets sample pictures from MilkDrop's textures folder (worms, cells,
// seaweed...). Those pictures have no licence that allows shipping them, so
// none are packaged. Butterchurn, which LG Afterglow uses, draws its clouds
// image for any texture it has not loaded; projectM would draw black. Every
// missing texture gets this instead: soft grey clouds that tile seamlessly,
// generated here, so no third-party image is involved.
constexpr int kCloudTextureSize = 128;

// RGBA8, kCloudTextureSize x kCloudTextureSize, grey with opaque alpha. The
// same pixels on every call.
std::vector<std::uint8_t> cloud_texture();
} // namespace hui::afterglow
