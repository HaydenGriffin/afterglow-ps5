// Afterglow - Headless MilkDrop capture: one picture per preset on synthetic music.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <cstddef>

namespace hui::afterglow
{
class Milkdrop;
}

namespace hui::host
{
// Advances the visualiser with synthetic stereo audio and a fixed animation clock.
void advance_milkdrop(afterglow::Milkdrop &milkdrop, std::size_t &sample, int frames);

// Renders count presets from presets/index.tsv through projectM, writing
// output/milkdrop-NN.png for each. Needs a current GL context.
int capture_milkdrop(const std::string &presets, const std::string &output, int count);
} // namespace hui::host
