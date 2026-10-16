// Afterglow - Bounded background thumbnail cache for Browse and Queue rows.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <pthread.h>
#include <string>
#include <unordered_map>
#include <vector>

// Usage (UI thread):
//   ThumbnailOptions options;
//   options.origin = "http://" + host + ":" + std::to_string(port);
//   ThumbnailCache cache(fetch_image, decode_image, options);  // artwork.hpp
//   cache.start();
//   each frame:  cache.pump(upload, release);       // GL lives in the callbacks
//   per row:     std::uint32_t tex = cache.texture(track.artwork);  // 0 = not yet
//   at exit:     cache.stop(); cache.clear(release); cache.join();

namespace hui::afterglow
{
struct ThumbnailOptions
{
    // Prefix for relative "/imageproxy/..." paths (Browse items arrive unresolved).
    std::string origin;
    int max_side = 160;
    std::size_t max_textures = 48, uploads_per_pump = 2;
    std::size_t max_queue = 32, max_ready = 4, max_failures = 256;
    // A queued URL no row asked for within stale_us has scrolled away: drop it.
    std::int64_t stale_us = 1000000, retry_us = 30000000;
    // Microseconds; empty uses sys::monotonic_us. Tests drive time by hand.
    std::function<std::int64_t()> clock;
};

// Imageproxy URLs are rewritten to ask the server for a max_side thumbnail, so
// rows never download the 512 px now-playing size. Returns "" when unusable.
std::string thumbnail_source(const std::string &url, const std::string &origin, int max_side);

class ThumbnailCache
{
  public:
    using Fetch = std::function<bool(const std::string &url, const std::function<bool()> &cancelled,
                                     std::vector<std::uint8_t> &body)>;
    using Decode = std::function<bool(const std::vector<std::uint8_t> &body,
                                      std::vector<std::uint8_t> &rgba, int &width, int &height)>;
    using Upload = std::function<std::uint32_t(int width, int height, const std::uint8_t *rgba)>;
    using Release = std::function<void(std::uint32_t texture)>;

    ThumbnailCache(Fetch fetch, Decode decode, ThumbnailOptions options = {});
    // Stops and joins the worker. Textures are not released here (no GL): call clear().
    ~ThumbnailCache();
    ThumbnailCache(const ThumbnailCache &) = delete;
    ThumbnailCache &operator=(const ThumbnailCache &) = delete;
    bool start();
    void stop();
    void join();

    // UI thread only. Never blocks on the network: returns the texture or 0 and
    // records the request. Empty URLs and remembered failures return 0 at once.
    std::uint32_t texture(const std::string &url);
    // UI/GL thread, once per frame: uploads at most uploads_per_pump results and
    // releases least recently used textures beyond max_textures.
    void pump(const Upload &upload, const Release &release);
    // Releases every texture (before the GL context goes away).
    void clear(const Release &release);

    std::size_t textures() const;
    std::size_t queued() const;
    std::size_t ready() const;

  private:
    struct Request
    {
        std::string url;
        std::int64_t asked = 0;
    };
    struct Result
    {
        std::string url;
        bool ok = false;
        int width = 0, height = 0;
        std::vector<std::uint8_t> rgba;
    };
    struct Entry
    {
        std::uint32_t texture = 0;
        std::uint64_t used = 0;
        std::int64_t retry_at = 0;
    };
    static void *entry(void *self);
    void run();
    std::int64_t now() const;
    void request(const std::string &url, std::int64_t asked);
    bool next_request(std::string &url);
    void load(const std::string &url, Result &result) const;
    void store(Result &result, const Upload &upload, const Release &release);
    void evict(const Release &release);
    void forget_failures();

    Fetch fetch_;
    Decode decode_;
    ThumbnailOptions options_;
    mutable std::mutex mutex_;
    pthread_t thread_{};
    bool started_ = false;
    std::atomic<bool> stopping_{true};
    // Guarded by mutex_: shared with the worker.
    std::deque<Request> queue_;
    std::deque<Result> ready_;
    std::string loading_;
    // UI thread only.
    std::unordered_map<std::string, Entry> entries_;
    std::size_t texture_count_ = 0, failure_count_ = 0, inserted_this_frame_ = 0;
    std::uint64_t frame_ = 0;
};
} // namespace hui::afterglow
