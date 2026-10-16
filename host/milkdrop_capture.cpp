// Afterglow - Headless MilkDrop capture: one picture per preset on synthetic music.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "milkdrop_capture.hpp"
#include "afterglow/milkdrop.hpp"
#include "core/save_file.hpp"
#include <GL/glcorearb.h>
#include "../third_party/stb/stb_image_write.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace hui::host
{
namespace
{

constexpr int kRate = 48000;

// A kick on every beat at 120 bpm, a bass line and a hiss: enough for
// presets that key off bass, mids and treble to move.
void music(std::vector<float> &stereo, std::size_t first, std::size_t frames)
{
    stereo.resize(frames * 2);
    std::uint32_t noise = 0x12345678u;
    for (std::size_t i = 0; i < frames; ++i)
    {
        const double t = static_cast<double>(first + i) / kRate;
        const double beat = std::fmod(t, 0.5);
        const double kick = std::sin(2 * M_PI * 55 * beat) * std::exp(-beat * 12);
        const double bass = 0.35 * std::sin(2 * M_PI * (std::fmod(t, 2.0) < 1 ? 110 : 82.4) * t);
        noise = noise * 1664525u + 1013904223u;
        const double hiss = 0.08 * (static_cast<double>(noise >> 8) / (1 << 24) - 0.5);
        const auto value = static_cast<float>(0.7 * kick + bass + hiss);
        stereo[i * 2] = value;
        stereo[i * 2 + 1] = value;
    }
}
} // namespace

void advance_milkdrop(afterglow::Milkdrop &milkdrop, std::size_t &sample, int frames)
{
    std::vector<float> stereo;
    for (int frame = 0; frame < frames; ++frame)
    {
        music(stereo, sample, kRate / 60);
        sample += kRate / 60;
        milkdrop.feed(stereo.data(), stereo.size() / 2);
        milkdrop.render(static_cast<double>(sample) / kRate);
    }
}

int capture_milkdrop(const std::string &presets, const std::string &output, int count)
{
    const bool transitions = std::getenv("AFTERGLOW_MILKDROP_TRANSITIONS") != nullptr;
    const char *frames = std::getenv("AFTERGLOW_MILKDROP_FRAMES");
    const int frames_per_preset = frames ? std::clamp(std::atoi(frames), 1, 600) : 90;
    const char *width = std::getenv("AFTERGLOW_MILKDROP_WIDTH");
    const int kWidth = width ? std::clamp(std::atoi(width), 160, 1920) : 1280;
    const int kHeight = kWidth * 9 / 16;
    afterglow::Milkdrop milkdrop;
    if (!milkdrop.init(kWidth, kHeight, presets))
        return 1;
    if (count <= 0)
        return 1;
    count = std::min(count, static_cast<int>(milkdrop.preset_count()));
    save::ensure_directory(output);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(kWidth * kHeight * 4));
    stbi_flip_vertically_on_write(1);
    std::size_t sample = 0;
    int written = 0;
    for (int preset = 0; preset < count; ++preset)
    {
        if (transitions && preset > 0)
        {
            if (preset % 2 == 1)
                milkdrop.next();
            else
                milkdrop.previous();
        }
        else if (!milkdrop.select(static_cast<std::size_t>(preset)))
        {
            std::fprintf(stderr, "preset %d failed to load\n", preset);
            continue;
        }
        advance_milkdrop(milkdrop, sample, frames_per_preset);
        GLuint read = 0;
        glGenFramebuffers(1, &read);
        glBindFramebuffer(GL_FRAMEBUFFER, read);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               milkdrop.texture(), 0);
        glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &read);
        long lit = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4)
        {
            lit += pixels[i] + pixels[i + 1] + pixels[i + 2] > 24 ? 1 : 0;
            pixels[i + 3] = 255;
        }
        char name[32];
        std::snprintf(name, sizeof(name), "/milkdrop-%02d.png", preset);
        const std::string path = output + name;
        const auto error = glGetError();
        if (error == GL_NO_ERROR &&
            stbi_write_png(path.c_str(), kWidth, kHeight, 4, pixels.data(), kWidth * 4) != 0)
            ++written;
        std::fprintf(stderr, "wrote %s: \"%s\" lit=%.0f%% GL error 0x%x\n", path.c_str(),
                     milkdrop.preset_name().c_str(),
                     100.0 * static_cast<double>(lit) / (kWidth * kHeight), error);
    }
    return written == count ? 0 : 1;
}
} // namespace hui::host
