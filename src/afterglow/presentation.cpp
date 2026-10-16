// ps5-homebrew-ui - FFT of the audio actually sent to HDMI, off the audio thread.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/presentation.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace hui::afterglow
{
Presentation &presentation()
{
    static Presentation value;
    return value;
}

void Analyzer::push(const std::int16_t *samples, int frames)
{
    std::array<float, 2048> stereo{};
    while (frames > 0)
    {
        const int count = std::min(frames, 1024);
        for (int i = 0; i < count * 2; ++i)
            stereo[static_cast<std::size_t>(i)] = static_cast<float>(samples[i]) / 32768.0f;
        ring_.write(stereo.data(), static_cast<std::size_t>(count));
        samples += count * 2;
        frames -= count;
    }
}

// A kick or bass note lifts the lowest bands well above their recent average.
// The threshold is in the analyser's 60 dB scale: 0.12 is about 7 dB at the
// default sensitivity; doubling the sensitivity halves it.
void Analyzer::detect_beat(float low, float interval)
{
    since_beat_ += interval;
    const float threshold = sensitivity_ > 0 ? std::clamp(0.12f / sensitivity_, 0.03f, 1.0f) : 2;
    const bool rising = low > low_previous_;
    if (rising && low - low_average_ > threshold && since_beat_ > 0.2f && low > 0.2f)
    {
        beat_ = 1;
        since_beat_ = 0;
    }
    low_previous_ = low;
    low_average_ += (low - low_average_) * std::min(1.0f, interval * 2.5f);
}

std::array<float, 32> Analyzer::update(float dt)
{
    fresh_.clear();
    beat_ *= std::exp(-dt * 12.0f);
    elapsed_ += dt;
    if (elapsed_ < 1.0f / 30.0f)
        return levels_;
    const float decay = std::exp(-elapsed_ * 7.0f);
    const float interval = elapsed_;
    elapsed_ = 0;
    std::array<float, 4096> stereo{};
    std::size_t got = 0;
    std::size_t total = 0;
    while ((got = ring_.read(stereo.data(), 2048)) != 0)
    {
        std::move(window_.begin() + static_cast<std::ptrdiff_t>(got), window_.end(),
                  window_.begin());
        for (std::size_t i = 0; i < got; ++i)
            window_[2048 - got + i] = (stereo[i * 2] + stereo[i * 2 + 1]) * 0.5f;
        fresh_.insert(fresh_.end(), stereo.begin(),
                      stereo.begin() + static_cast<std::ptrdiff_t>(got * 2));
        total += got;
    }
    if (total == 0)
    {
        for (float &value : levels_)
            value *= decay;
        since_beat_ += interval;
        return levels_;
    }
    constexpr float pi = 3.14159265358979323846f;
    std::array<std::complex<float>, 2048> bins{};
    for (std::size_t i = 0; i < bins.size(); ++i)
        bins[i] = window_[i] * (0.5f - 0.5f * std::cos(2 * pi * static_cast<float>(i) / 2047));
    for (std::size_t i = 1, j = 0; i < bins.size(); ++i)
    {
        std::size_t bit = bins.size() >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(bins[i], bins[j]);
    }
    for (std::size_t len = 2; len <= bins.size(); len <<= 1)
    {
        const auto step = std::polar(1.0f, -2 * pi / static_cast<float>(len));
        for (std::size_t i = 0; i < bins.size(); i += len)
        {
            std::complex<float> phase{1, 0};
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto even = bins[i + j];
                const auto odd = bins[i + j + len / 2] * phase;
                bins[i + j] = even + odd;
                bins[i + j + len / 2] = even - odd;
                phase *= step;
            }
        }
    }
    // Bands 0-7 span about 40-180 Hz: kick and bass.
    float bass = 0;
    for (std::size_t band = 0; band < levels_.size(); ++band)
    {
        const float low = 40 * std::pow(400.0f, static_cast<float>(band) / 32);
        const float high = 40 * std::pow(400.0f, static_cast<float>(band + 1) / 32);
        const auto first = std::max<std::size_t>(1, static_cast<std::size_t>(low * 2048 / 48000));
        const auto last = std::min<std::size_t>(
            1024, std::max(first + 1, static_cast<std::size_t>(high * 2048 / 48000)));
        float peak = 0;
        for (std::size_t bin = first; bin < last; ++bin)
            peak = std::max(peak, std::abs(bins[bin]) / 512.0f);
        const float value =
            std::clamp((20 * std::log10(std::max(peak, 0.00001f)) + 60) / 60, 0.0f, 1.0f);
        levels_[band] = std::max(value, levels_[band] * decay);
        if (band < 8)
            bass += value / 8;
    }
    detect_beat(bass, interval);
    return levels_;
}
} // namespace hui::afterglow
