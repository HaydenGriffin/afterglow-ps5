// Afterglow - Headless MilkDrop capture: one picture per preset on synthetic music.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "milkdrop_capture.hpp"
#include "afterglow/milkdrop.hpp"
#include "afterglow/artwork.hpp"
#include "afterglow/cover_texture.hpp"
#include "core/save_file.hpp"
#include <GL/glcorearb.h>
#include "../third_party/stb/stb_image_write.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <utility>
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
// AFTERGLOW_MILKDROP_ONLY=<i,j,...>: capture these preset indices only.
std::vector<int> picks(int count)
{
    std::vector<int> out;
    if (const char *only = std::getenv("AFTERGLOW_MILKDROP_ONLY"))
    {
        std::string list = only;
        std::size_t at = 0;
        while (at < list.size())
        {
            const auto comma = list.find(',', at);
            out.push_back(std::atoi(list.substr(at, comma - at).c_str()));
            at = comma == std::string::npos ? list.size() : comma + 1;
        }
        return out;
    }
    for (int i = 0; i < count; ++i)
        out.push_back(i);
    return out;
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

namespace
{
// The current frame as output/milkdrop-<name>.png; false on a GL or file error.
bool write_picture(afterglow::Milkdrop &milkdrop, const std::string &output,
                   const std::string &name, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4));
    GLuint read = 0;
    glGenFramebuffers(1, &read);
    glBindFramebuffer(GL_FRAMEBUFFER, read);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, milkdrop.texture(),
                           0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &read);
    long lit = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4)
    {
        lit += pixels[i] + pixels[i + 1] + pixels[i + 2] > 24 ? 1 : 0;
        pixels[i + 3] = 255;
    }
    const std::string path = output + "/milkdrop-" + name + ".png";
    const auto error = glGetError();
    const bool ok = error == GL_NO_ERROR &&
                    stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
    std::fprintf(stderr, "wrote %s: \"%s\" lit=%.0f%% GL error 0x%x\n", path.c_str(),
                 milkdrop.preset_name().c_str(),
                 100.0 * static_cast<double>(lit) / (width * height), error);
    return ok;
}

// Renders on synthetic music for `seconds` of real time: the cover's
// schedule runs on the real clock, as on the console.
void run_for(afterglow::Milkdrop &milkdrop, std::size_t &sample, double seconds)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until)
    {
        advance_milkdrop(milkdrop, sample, 1);
        usleep(10000);
    }
}

// AFTERGLOW_MILKDROP_COVER=<png>: the picture the presets sample. It fades in
// over the preset already running (so the texture is updated in place, as
// for a new song), with milkdrop-fade.png halfway through the fade.
bool show_cover(afterglow::Milkdrop &milkdrop, std::size_t &sample, const std::string &output,
                int width, int height)
{
    const char *path = std::getenv("AFTERGLOW_MILKDROP_COVER");
    if (path == nullptr)
        return true;
    std::string file;
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    if (!save::read_file(path, &file, 8u << 20) ||
        !afterglow::decode_image(std::vector<std::uint8_t>(file.begin(), file.end()), rgba, w, h))
    {
        std::fprintf(stderr, "cover %s unreadable\n", path);
        return false;
    }
    milkdrop.set_cover(afterglow::cover_pixels(rgba, w, h));
    // 1 s to settle, then half of the 2 s fade.
    run_for(milkdrop, sample, 2.05);
    const bool ok = write_picture(milkdrop, output, "fade", width, height);
    run_for(milkdrop, sample, 1.2);
    return ok;
}
} // namespace

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
    stbi_flip_vertically_on_write(1);
    std::size_t sample = 0;
    int written = 0;
    const auto chosen = picks(count);
    bool cover_pending = true;
    for (const int preset : chosen)
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
        if (std::exchange(cover_pending, false) &&
            !show_cover(milkdrop, sample, output, kWidth, kHeight))
            return 1;
        char name[16];
        std::snprintf(name, sizeof(name), "%02d", preset);
        written += write_picture(milkdrop, output, name, kWidth, kHeight) ? 1 : 0;
    }
    return written == static_cast<int>(chosen.size()) ? 0 : 1;
}
} // namespace hui::host
