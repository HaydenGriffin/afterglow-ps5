// Afterglow - Bounded background artwork download, decode and colour extraction.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

namespace hui::afterglow
{
struct ArtworkResult
{
    std::uint64_t generation = 0;
    std::string url;
    // False when the URL was empty or the cover could not be fetched or
    // decoded: the colours below are then defaults, not the cover's.
    bool ok = false;
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
    std::array<float, 3> accent{1, 1, 1}, secondary{1, 1, 1};
    // The cover as MilkDrop presets sample it (cover_pixels), made on the
    // worker so the frame thread only copies it into the texture.
    std::vector<std::uint8_t> cover;
};

// GET of an http:// image (IPv4 literal host) on a non-blocking socket, bounded
// in size and to 8 s; cancelled() is polled every 50 ms. Shared with thumbnails.
// Defined in artwork_fetch.cpp, which needs projectM's stb_image.
bool fetch_image(const std::string &url, const std::function<bool()> &cancelled,
                 std::vector<std::uint8_t> &body);
// JPEG or PNG to RGBA8, refusing anything over 2048 px a side before allocating.
bool decode_image(const std::vector<std::uint8_t> &body, std::vector<std::uint8_t> &rgba,
                  int &width, int &height);

struct ArtworkOptions
{
    // A cover that failed is asked for again this often, this many times
    // (a server busy at the track change); a new URL starts the count again.
    std::int64_t retry_us = 10000000;
    int retries = 3;
    // Microseconds; empty uses sys::monotonic_us. Tests drive time by hand.
    std::function<std::int64_t()> clock;
};

// Usage: Artwork artwork(fetch_image, decode_image); artwork.start();
// every frame request(url), then take_result() for the generation it returned.
class Artwork
{
  public:
    using Fetch = std::function<bool(const std::string &url, const std::function<bool()> &cancelled,
                                     std::vector<std::uint8_t> &body)>;
    using Decode = std::function<bool(const std::vector<std::uint8_t> &body,
                                      std::vector<std::uint8_t> &rgba, int &width, int &height)>;
    Artwork(Fetch fetch, Decode decode, ArtworkOptions options = {});
    ~Artwork();
    Artwork(const Artwork &) = delete;
    Artwork &operator=(const Artwork &) = delete;
    bool start();
    // Same URL is deduplicated, except for a bounded retry after a failure.
    // Empty URL clears the result. No GL runs on this worker.
    std::uint64_t request(std::string url);
    bool take_result(ArtworkResult &result);
    std::string status() const;
    void stop();
    void join();

  private:
    static void *entry(void *self);
    void run();
    bool cancelled(std::uint64_t generation) const;
    bool retry_due() const;
    bool decode(const std::vector<std::uint8_t> &body, ArtworkResult &result) const;
    Fetch fetch_;
    Decode decode_;
    ArtworkOptions options_;
    mutable std::mutex mutex_;
    pthread_t thread_{};
    bool started_ = false, ready_ = false;
    std::atomic<bool> stopping_{true};
    std::atomic<std::uint64_t> generation_{0};
    std::string requested_, status_ = "No artwork";
    ArtworkResult result_;
    // The requested URL's last load failed at failed_at_; retried_ times so far.
    bool failed_ = false;
    std::int64_t failed_at_ = 0;
    int retried_ = 0;
};
} // namespace hui::afterglow
