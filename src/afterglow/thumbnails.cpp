// Afterglow - Bounded background thumbnail cache for Browse and Queue rows.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/thumbnails.hpp"
#include "platform/ps5/system.hpp"
#include <algorithm>
#include <unistd.h>
#include <utility>

namespace hui::afterglow
{
namespace
{
// Decoders cap images at 2048 px a side, so int offsets cannot overflow.
std::size_t pixel(int x, int y, int stride)
{
    return static_cast<std::size_t>((y * stride + x) * 4);
}
// Area average rather than point sampling: a 512 px cover point-sampled to 80 px
// on screen shimmers, and the worker has the time to do it properly.
void downscale(const std::vector<std::uint8_t> &source, int width, int height, int max_side,
               int &out_width, int &out_height, std::vector<std::uint8_t> &out)
{
    const int longest = std::max(width, height);
    out_width = std::max(1, width * max_side / longest);
    out_height = std::max(1, height * max_side / longest);
    out.resize(pixel(0, out_height, out_width));
    for (int y = 0; y < out_height; ++y)
    {
        const int y0 = y * height / out_height;
        const int y1 = std::max(y0 + 1, (y + 1) * height / out_height);
        for (int x = 0; x < out_width; ++x)
        {
            const int x0 = x * width / out_width;
            const int x1 = std::max(x0 + 1, (x + 1) * width / out_width);
            std::uint32_t sum[4]{};
            for (int sy = y0; sy < y1; ++sy)
                for (int sx = x0; sx < x1; ++sx)
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        sum[channel] += source[pixel(sx, sy, width) + channel];
            const auto count = static_cast<std::uint32_t>((x1 - x0) * (y1 - y0));
            for (std::size_t channel = 0; channel < 4; ++channel)
                out[pixel(x, y, out_width) + channel] =
                    static_cast<std::uint8_t>((sum[channel] + count / 2) / count);
        }
    }
}
} // namespace

std::string thumbnail_source(const std::string &url, const std::string &origin, int max_side)
{
    std::string source = url;
    if (source.starts_with("/"))
    {
        if (origin.empty())
            return {};
        source = origin + source;
    }
    if (source.find("/imageproxy/") == std::string::npos)
        return source;
    const std::string size = "size=" + std::to_string(max_side);
    const auto query = source.find('?');
    if (query == std::string::npos)
        return source + "?" + size;
    for (auto at = source.find("size=", query); at != std::string::npos;
         at = source.find("size=", at + 1))
    {
        if (source[at - 1] != '?' && source[at - 1] != '&')
            continue;
        const auto end = source.find('&', at);
        source.replace(at, (end == std::string::npos ? source.size() : end) - at, size);
        return source;
    }
    return source + "&" + size;
}

ThumbnailCache::ThumbnailCache(Fetch fetch, Decode decode, ThumbnailOptions options)
    : fetch_(std::move(fetch)), decode_(std::move(decode)), options_(std::move(options))
{
    options_.max_side = std::max(1, options_.max_side);
    if (!options_.clock)
        options_.clock = [] { return sys::monotonic_us(); };
}
ThumbnailCache::~ThumbnailCache()
{
    stop();
    join();
}
bool ThumbnailCache::start()
{
    if (started_)
        return false;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
        return false;
    stopping_ = false;
    // The fetch keeps a 16 KiB receive buffer on the stack; stb_image's decoders
    // add a few more. 512 KiB leaves headroom without the 1 MiB of Artwork.
    const int stack = pthread_attr_setstacksize(&attr, 512 * 1024);
    started_ = stack == 0 && pthread_create(&thread_, &attr, entry, this) == 0;
    pthread_attr_destroy(&attr);
    if (!started_)
        stopping_ = true;
    return started_;
}
void ThumbnailCache::stop()
{
    stopping_ = true;
}
void ThumbnailCache::join()
{
    if (started_)
    {
        pthread_join(thread_, nullptr);
        started_ = false;
    }
}

std::int64_t ThumbnailCache::now() const
{
    return options_.clock();
}

std::uint32_t ThumbnailCache::texture(const std::string &url)
{
    if (url.empty())
        return 0;
    const auto current = now();
    const auto found = entries_.find(url);
    if (found != entries_.end())
    {
        if (found->second.texture)
        {
            found->second.used = frame_;
            return found->second.texture;
        }
        if (current < found->second.retry_at)
            return 0;
        entries_.erase(found);
        --failure_count_;
    }
    request(url, current);
    return 0;
}

void ThumbnailCache::request(const std::string &url, std::int64_t asked)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (url == loading_)
        return;
    for (const auto &result : ready_)
        if (result.url == url)
            return;
    for (auto &queued : queue_)
        if (queued.url == url)
        {
            queued.asked = asked;
            return;
        }
    // Newest frame first, but rows first seen in the same frame keep their
    // top-to-bottom order, so a fresh screen fills from the top.
    const auto at = std::min(inserted_this_frame_, queue_.size());
    queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(at), Request{url, asked});
    ++inserted_this_frame_;
    if (queue_.size() > options_.max_queue)
        queue_.pop_back();
}

bool ThumbnailCache::next_request(std::string &url)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Back-pressure: decoded pixels wait here until the UI uploads them, so a UI
    // that stops pumping must stop the downloads too.
    if (ready_.size() >= options_.max_ready)
        return false;
    const auto oldest = now() - options_.stale_us;
    std::erase_if(queue_, [&](const Request &queued) { return queued.asked < oldest; });
    if (queue_.empty())
        return false;
    url = std::move(queue_.front().url);
    queue_.pop_front();
    loading_ = url;
    return true;
}

void *ThumbnailCache::entry(void *self)
{
    static_cast<ThumbnailCache *>(self)->run();
    return nullptr;
}
void ThumbnailCache::run()
{
    while (!stopping_)
    {
        std::string url;
        if (!next_request(url))
        {
            usleep(20000);
            continue;
        }
        Result result;
        result.url = url;
        load(url, result);
        std::lock_guard<std::mutex> lock(mutex_);
        loading_.clear();
        if (!stopping_)
            ready_.push_back(std::move(result));
    }
}

void ThumbnailCache::load(const std::string &url, Result &result) const
{
    const auto source = thumbnail_source(url, options_.origin, options_.max_side);
    std::vector<std::uint8_t> body, rgba;
    int width = 0, height = 0;
    const auto cancelled = [this] { return stopping_.load(); };
    if (source.empty() || !fetch_(source, cancelled, body) || stopping_ ||
        !decode_(body, rgba, width, height) || width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        return;
    if (std::max(width, height) <= options_.max_side)
    {
        result.width = width;
        result.height = height;
        result.rgba = std::move(rgba);
    }
    else
        downscale(rgba, width, height, options_.max_side, result.width, result.height, result.rgba);
    result.ok = true;
}

void ThumbnailCache::pump(const Upload &upload, const Release &release)
{
    ++frame_;
    inserted_this_frame_ = 0;
    std::vector<Result> results;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!ready_.empty() && results.size() < options_.uploads_per_pump)
        {
            results.push_back(std::move(ready_.front()));
            ready_.pop_front();
        }
    }
    for (auto &result : results)
        store(result, upload, release);
    evict(release);
    forget_failures();
}

void ThumbnailCache::store(Result &result, const Upload &upload, const Release &release)
{
    Entry entry;
    entry.used = frame_;
    if (result.ok)
        entry.texture = upload(result.width, result.height, result.rgba.data());
    // A refused upload is a failure too: retrying it every frame would not help.
    if (!entry.texture)
        entry.retry_at = now() + options_.retry_us;
    const auto [slot, inserted] = entries_.try_emplace(result.url, entry);
    if (!inserted)
    {
        // Requests are deduplicated, so this is defensive: never leak a second texture.
        if (entry.texture)
            release(entry.texture);
        return;
    }
    if (entry.texture)
        ++texture_count_;
    else
        ++failure_count_;
}

void ThumbnailCache::evict(const Release &release)
{
    while (texture_count_ > options_.max_textures)
    {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (it->second.texture &&
                (oldest == entries_.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == entries_.end())
            return;
        release(oldest->second.texture);
        entries_.erase(oldest);
        --texture_count_;
    }
}

void ThumbnailCache::forget_failures()
{
    while (failure_count_ > options_.max_failures)
    {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if (!it->second.texture &&
                (oldest == entries_.end() || it->second.retry_at < oldest->second.retry_at))
                oldest = it;
        if (oldest == entries_.end())
            return;
        entries_.erase(oldest);
        --failure_count_;
    }
}

void ThumbnailCache::clear(const Release &release)
{
    for (const auto &[url, entry] : entries_)
        if (entry.texture)
            release(entry.texture);
    entries_.clear();
    texture_count_ = failure_count_ = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    ready_.clear();
}

std::size_t ThumbnailCache::textures() const
{
    return texture_count_;
}
std::size_t ThumbnailCache::queued() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}
std::size_t ThumbnailCache::ready() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_.size();
}
} // namespace hui::afterglow
