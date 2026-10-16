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
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
    std::array<float, 3> accent{1, 1, 1}, secondary{1, 1, 1};
    // The cover as MilkDrop presets sample it (cover_pixels), made on the
    // worker so the frame thread only copies it into the texture.
    std::vector<std::uint8_t> cover;
};

// GET of an http:// image (IPv4 literal host) on a non-blocking socket, bounded
// in size and to 8 s; cancelled() is polled every 50 ms. Shared with thumbnails.
bool fetch_image(const std::string &url, const std::function<bool()> &cancelled,
                 std::vector<std::uint8_t> &body);
// JPEG or PNG to RGBA8, refusing anything over 2048 px a side before allocating.
bool decode_image(const std::vector<std::uint8_t> &body, std::vector<std::uint8_t> &rgba,
                  int &width, int &height);

class Artwork
{
  public:
    Artwork() = default;
    ~Artwork();
    Artwork(const Artwork &) = delete;
    Artwork &operator=(const Artwork &) = delete;
    bool start();
    // Same URL is deduplicated. Empty URL clears the result. No GL runs on this worker.
    std::uint64_t request(std::string url);
    bool take_result(ArtworkResult &result);
    std::string status() const;
    void stop();
    void join();

  private:
    static void *entry(void *self);
    void run();
    bool cancelled(std::uint64_t generation) const;
    static bool decode(const std::vector<std::uint8_t> &body, ArtworkResult &result);
    mutable std::mutex mutex_;
    pthread_t thread_{};
    bool started_ = false, ready_ = false;
    std::atomic<bool> stopping_{true};
    std::atomic<std::uint64_t> generation_{0};
    std::string requested_, status_ = "No artwork";
    ArtworkResult result_;
};
} // namespace hui::afterglow
