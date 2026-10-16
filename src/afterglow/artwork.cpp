// Afterglow - Bounded background artwork download, decode and colour extraction.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/artwork.hpp"
#include "afterglow/cover_texture.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <unistd.h>

namespace hui::afterglow
{
Artwork::Artwork(Fetch fetch, Decode decode, ArtworkOptions options)
    : fetch_(std::move(fetch)), decode_(std::move(decode)), options_(std::move(options))
{
    if (!options_.clock)
        options_.clock = [] { return sys::monotonic_us(); };
}
Artwork::~Artwork()
{
    stop();
    join();
}
bool Artwork::start()
{
    if (started_)
        return false;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
        return false;
    stopping_ = false;
    const int stack = pthread_attr_setstacksize(&attr, 1024 * 1024);
    started_ = stack == 0 && pthread_create(&thread_, &attr, entry, this) == 0;
    pthread_attr_destroy(&attr);
    if (!started_)
        stopping_ = true;
    return started_;
}
void Artwork::stop()
{
    stopping_ = true;
}
void Artwork::join()
{
    if (started_)
    {
        pthread_join(thread_, nullptr);
        started_ = false;
    }
}
std::uint64_t Artwork::request(std::string url)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (url == requested_ && !retry_due())
        return generation_;
    if (url == requested_)
        ++retried_;
    else
        retried_ = 0;
    requested_ = std::move(url);
    failed_ = false;
    ++generation_;
    ready_ = false;
    status_ = requested_.empty() ? "No artwork" : "Loading artwork";
    return generation_;
}
bool Artwork::take_result(ArtworkResult &result)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_)
        return false;
    result = std::move(result_);
    ready_ = false;
    return true;
}
std::string Artwork::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
bool Artwork::retry_due() const
{
    return failed_ && retried_ < options_.retries &&
           options_.clock() - failed_at_ >= options_.retry_us;
}
bool Artwork::cancelled(std::uint64_t generation) const
{
    return stopping_ || generation != generation_;
}
void *Artwork::entry(void *self)
{
    static_cast<Artwork *>(self)->run();
    return nullptr;
}
void Artwork::run()
{
    std::uint64_t processed = 0;
    while (!stopping_)
    {
        ArtworkResult next;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            next.generation = generation_;
            next.url = requested_;
        }
        if (next.generation == processed)
        {
            usleep(50000);
            continue;
        }
        processed = next.generation;
        std::vector<std::uint8_t> body;
        const auto superseded = [&] { return cancelled(processed); };
        next.ok = !next.url.empty() && fetch_(next.url, superseded, body) &&
                  !cancelled(processed) && decode(body, next);
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled(processed))
            continue;
        status_ = next.url.empty() ? "No artwork"
                  : next.ok        ? "Artwork ready"
                                   : "Artwork unavailable";
        failed_ = !next.url.empty() && !next.ok;
        failed_at_ = options_.clock();
        result_ = std::move(next);
        ready_ = true;
    }
}

bool Artwork::decode(const std::vector<std::uint8_t> &body, ArtworkResult &result) const
{
    std::vector<std::uint8_t> decoded;
    int width = 0, height = 0;
    if (!decode_(body, decoded, width, height) || width <= 0 || height <= 0 ||
        decoded.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        return false;
    const auto *pixels = decoded.data();
    const int divisor = (std::max(width, height) + 511) / 512;
    result.width = std::max(1, width / divisor);
    result.height = std::max(1, height / divisor);
    result.rgba.resize(static_cast<std::size_t>(result.width * result.height) * 4);
    std::array<double, 3> sum{};
    double total = 0;
    for (int y = 0; y < result.height; ++y)
        for (int x = 0; x < result.width; ++x)
        {
            const auto source = static_cast<std::size_t>((y * divisor * width + x * divisor) * 4);
            const auto destination = static_cast<std::size_t>((y * result.width + x) * 4);
            std::copy_n(pixels + source, 4, result.rgba.data() + destination);
            const auto maximum = std::max({pixels[source], pixels[source + 1], pixels[source + 2]});
            const auto minimum = std::min({pixels[source], pixels[source + 1], pixels[source + 2]});
            const double weight = (1.0 + maximum - minimum) * pixels[source + 3] / 255.0;
            total += weight;
            for (std::size_t channel = 0; channel < 3; ++channel)
                sum[channel] += pixels[source + channel] * weight;
        }
    // A wholly transparent picture has no colour to give.
    if (total <= 0)
        return false;
    for (std::size_t channel = 0; channel < 3; ++channel)
    {
        result.accent[channel] = static_cast<float>(sum[channel] / (total * 255));
        result.secondary[channel] = result.accent[channel] * 0.45f;
    }
    result.cover = cover_pixels(result.rgba, result.width, result.height);
    return true;
}
} // namespace hui::afterglow
