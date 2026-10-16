// Afterglow - Thumbnail cache scheduling, upload budget and eviction tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/thumbnails.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using hui::afterglow::thumbnail_source;
using hui::afterglow::ThumbnailCache;
using hui::afterglow::ThumbnailOptions;

namespace
{
template <typename Predicate> bool eventually(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

// The network and the decoder are stand-ins: URLs containing "missing" fail to
// download, "corrupt" fails to decode, "block" waits until released or stopped.
struct Fixture
{
    std::atomic<std::int64_t> clock{0};
    std::atomic<bool> release_block{false};
    std::mutex mutex;
    std::vector<std::string> fetched;
    std::vector<std::uint32_t> released;
    std::vector<std::pair<int, int>> uploaded;
    std::uint32_t next_texture = 0;

    ThumbnailOptions options()
    {
        ThumbnailOptions options;
        options.origin = "http://10.0.0.2:8095";
        options.clock = [this] { return clock.load(); };
        return options;
    }
    ThumbnailCache::Fetch fetch()
    {
        return [this](const std::string &url, const std::function<bool()> &cancelled,
                      std::vector<std::uint8_t> &body)
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                fetched.push_back(url);
            }
            if (url.find("block") != std::string::npos)
                while (!release_block && !cancelled())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (url.find("missing") != std::string::npos || cancelled())
                return false;
            body.assign(url.begin(), url.end());
            return true;
        };
    }
    static ThumbnailCache::Decode decode()
    {
        return [](const std::vector<std::uint8_t> &body, std::vector<std::uint8_t> &rgba,
                  int &width, int &height)
        {
            if (std::string(body.begin(), body.end()).find("corrupt") != std::string::npos)
                return false;
            width = 320;
            height = 160;
            rgba.assign(static_cast<std::size_t>(width * height) * 4, 200);
            return true;
        };
    }
    ThumbnailCache::Upload upload()
    {
        return [this](int width, int height, const std::uint8_t *)
        {
            uploaded.emplace_back(width, height);
            return ++next_texture;
        };
    }
    ThumbnailCache::Release release()
    {
        return [this](std::uint32_t texture) { released.push_back(texture); };
    }
    std::size_t fetch_count()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return fetched.size();
    }
    std::vector<std::string> fetch_log()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return fetched;
    }
};

struct CacheTest : ::testing::Test
{
    Fixture fx;
};
} // namespace

TEST(AfterglowThumbnails, AsksImageproxyForThumbnailSizeAndResolvesRelativePaths)
{
    const std::string origin = "http://10.0.0.2:8095";
    EXPECT_EQ(thumbnail_source("/imageproxy/abc?size=512&fmt=png", origin, 160),
              "http://10.0.0.2:8095/imageproxy/abc?size=160&fmt=png");
    EXPECT_EQ(thumbnail_source("http://h:1/imageproxy/abc?fmt=png&size=512", origin, 160),
              "http://h:1/imageproxy/abc?fmt=png&size=160");
    EXPECT_EQ(thumbnail_source("http://h:1/imageproxy/abc?fmt=png", origin, 160),
              "http://h:1/imageproxy/abc?fmt=png&size=160");
    EXPECT_EQ(thumbnail_source("http://h:1/imageproxy/abc", origin, 160),
              "http://h:1/imageproxy/abc?size=160");
    EXPECT_EQ(thumbnail_source("http://h:1/imageproxy/abc?thumbsize=9", origin, 160),
              "http://h:1/imageproxy/abc?thumbsize=9&size=160");
    EXPECT_EQ(thumbnail_source("http://h:1/cover.jpg", origin, 160), "http://h:1/cover.jpg");
    EXPECT_EQ(thumbnail_source("/imageproxy/abc", "", 160), "");
}

TEST_F(CacheTest, ReturnsZeroUntilPumpedThenTheUploadedTexture)
{
    ThumbnailCache cache(fx.fetch(), Fixture::decode(), fx.options());
    ASSERT_TRUE(cache.start());
    EXPECT_EQ(cache.texture(""), 0U);
    const std::string url = "/imageproxy/a?size=512&fmt=png";
    EXPECT_EQ(cache.texture(url), 0U);
    ASSERT_TRUE(eventually([&] { return cache.ready() == 1; }));
    EXPECT_EQ(cache.texture(url), 0U) << "results wait for the GL thread's pump";
    cache.pump(fx.upload(), fx.release());
    EXPECT_EQ(cache.texture(url), 1U);
    EXPECT_EQ(cache.texture(url), 1U);
    ASSERT_EQ(fx.uploaded.size(), 1U);
    EXPECT_EQ(fx.uploaded[0], std::make_pair(160, 80)) << "downscaled on the worker";
    ASSERT_EQ(fx.fetch_log().size(), 1U);
    EXPECT_EQ(fx.fetch_log()[0], "http://10.0.0.2:8095/imageproxy/a?size=160&fmt=png");
    cache.stop();
    cache.clear(fx.release());
    cache.join();
    EXPECT_EQ(fx.released, std::vector<std::uint32_t>{1});
}

TEST_F(CacheTest, EvictsTheLeastRecentlyUsedTextureBeyondTheCap)
{
    auto options = fx.options();
    options.max_textures = 2;
    options.uploads_per_pump = 8;
    ThumbnailCache cache(fx.fetch(), Fixture::decode(), options);
    ASSERT_TRUE(cache.start());
    cache.texture("http://h/a.png");
    cache.texture("http://h/b.png");
    ASSERT_TRUE(eventually([&] { return cache.ready() == 2; }));
    cache.pump(fx.upload(), fx.release());
    const auto a = cache.texture("http://h/a.png"), b = cache.texture("http://h/b.png");
    ASSERT_NE(a, 0U);
    ASSERT_NE(b, 0U);
    cache.pump(fx.upload(), fx.release());
    EXPECT_EQ(cache.texture("http://h/a.png"), a);
    cache.texture("http://h/c.png");
    ASSERT_TRUE(eventually([&] { return cache.ready() == 1; }));
    cache.pump(fx.upload(), fx.release());
    EXPECT_EQ(fx.released, std::vector<std::uint32_t>{b});
    EXPECT_EQ(cache.textures(), 2U);
    EXPECT_EQ(cache.texture("http://h/a.png"), a);
    EXPECT_EQ(cache.texture("http://h/b.png"), 0U) << "evicted, so asked for again";
}

TEST_F(CacheTest, RemembersFailuresUntilTheRetryInterval)
{
    auto options = fx.options();
    options.retry_us = 30000000;
    ThumbnailCache cache(fx.fetch(), Fixture::decode(), options);
    ASSERT_TRUE(cache.start());
    cache.texture("http://h/missing.png");
    cache.texture("http://h/corrupt.png");
    ASSERT_TRUE(eventually([&] { return cache.ready() == 2; }));
    cache.pump(fx.upload(), fx.release());
    EXPECT_TRUE(fx.uploaded.empty());
    for (int frame = 0; frame < 10; ++frame)
    {
        EXPECT_EQ(cache.texture("http://h/missing.png"), 0U);
        EXPECT_EQ(cache.texture("http://h/corrupt.png"), 0U);
        cache.pump(fx.upload(), fx.release());
    }
    EXPECT_EQ(cache.queued(), 0U);
    EXPECT_EQ(fx.fetch_count(), 2U);
    fx.clock = 31000000;
    cache.texture("http://h/missing.png");
    EXPECT_TRUE(eventually([&] { return fx.fetch_count() == 3; }));
}

TEST_F(CacheTest, DropsRequestsNoRowAskedForRecently)
{
    ThumbnailCache cache(fx.fetch(), Fixture::decode(), fx.options());
    ASSERT_TRUE(cache.start());
    cache.texture("http://h/block.png");
    ASSERT_TRUE(eventually([&] { return fx.fetch_count() == 1; }));
    cache.texture("http://h/scrolled-past-1.png");
    cache.texture("http://h/scrolled-past-2.png");
    fx.clock = 2000000;
    cache.texture("http://h/visible.png");
    fx.release_block = true;
    ASSERT_TRUE(eventually(
        [&]
        {
            cache.texture("http://h/visible.png");
            cache.pump(fx.upload(), fx.release());
            return cache.texture("http://h/visible.png") != 0;
        }));
    EXPECT_EQ(fx.fetch_log(),
              (std::vector<std::string>{"http://h/block.png", "http://h/visible.png"}));
    EXPECT_EQ(cache.queued(), 0U);
}

TEST_F(CacheTest, UploadsAtMostTheBudgetPerPump)
{
    auto options = fx.options();
    options.uploads_per_pump = 2;
    options.max_ready = 8;
    ThumbnailCache cache(fx.fetch(), Fixture::decode(), options);
    ASSERT_TRUE(cache.start());
    for (int i = 0; i < 5; ++i)
        cache.texture("http://h/" + std::to_string(i) + ".png");
    ASSERT_TRUE(eventually([&] { return cache.ready() == 5; }));
    cache.pump(fx.upload(), fx.release());
    EXPECT_EQ(fx.uploaded.size(), 2U);
    cache.pump(fx.upload(), fx.release());
    EXPECT_EQ(fx.uploaded.size(), 4U);
    cache.pump(fx.upload(), fx.release());
    EXPECT_EQ(fx.uploaded.size(), 5U);
}

TEST_F(CacheTest, BoundsTheQueueAndTheDecodedBacklog)
{
    auto options = fx.options();
    options.max_queue = 3;
    options.max_ready = 2;
    ThumbnailCache cache(fx.fetch(), Fixture::decode(), options);
    for (int i = 0; i < 10; ++i)
        cache.texture("http://h/" + std::to_string(i) + ".png");
    EXPECT_EQ(cache.queued(), 3U);
    ASSERT_TRUE(cache.start());
    ASSERT_TRUE(eventually([&] { return cache.ready() == 2; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_EQ(cache.ready(), 2U) << "an unpumped UI stops the downloads";
    EXPECT_EQ(cache.queued(), 1U);
}

TEST_F(CacheTest, StopsAndJoinsWithRequestsPendingAndInFlight)
{
    ThumbnailCache cache(fx.fetch(), Fixture::decode(), fx.options());
    ASSERT_TRUE(cache.start());
    cache.texture("http://h/block.png");
    for (int i = 0; i < 5; ++i)
        cache.texture("http://h/" + std::to_string(i) + ".png");
    ASSERT_TRUE(eventually([&] { return fx.fetch_count() == 1; }));
    cache.stop();
    cache.join();
    EXPECT_EQ(fx.fetch_count(), 1U);
    EXPECT_EQ(cache.ready(), 0U);
}

TEST_F(CacheTest, DestructorStopsABusyWorker)
{
    {
        ThumbnailCache cache(fx.fetch(), Fixture::decode(), fx.options());
        ASSERT_TRUE(cache.start());
        cache.texture("http://h/block.png");
        ASSERT_TRUE(eventually([&] { return fx.fetch_count() == 1; }));
    }
    EXPECT_EQ(fx.fetch_count(), 1U);
}
