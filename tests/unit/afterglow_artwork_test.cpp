// Afterglow - Now-playing artwork worker: results, failures and bounded retries.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/artwork.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using hui::afterglow::Artwork;
using hui::afterglow::ArtworkOptions;
using hui::afterglow::ArtworkResult;

namespace
{
// URLs containing "missing" fail to download; anything else decodes to a
// 2 x 2 picture of one colour, red unless the URL says "clear" (transparent).
struct Fixture
{
    std::atomic<std::int64_t> clock{0};
    std::atomic<int> fetches{0};

    ArtworkOptions options()
    {
        ArtworkOptions options;
        options.clock = [this] { return clock.load(); };
        return options;
    }
    Artwork::Fetch fetch()
    {
        return [this](const std::string &url, const std::function<bool()> &,
                      std::vector<std::uint8_t> &body)
        {
            ++fetches;
            if (url.find("missing") != std::string::npos)
                return false;
            body.assign(url.begin(), url.end());
            return true;
        };
    }
    static Artwork::Decode decode()
    {
        return [](const std::vector<std::uint8_t> &body, std::vector<std::uint8_t> &rgba,
                  int &width, int &height)
        {
            const std::string url(body.begin(), body.end());
            const std::uint8_t alpha = url.find("clear") != std::string::npos ? 0 : 255;
            width = height = 2;
            rgba.clear();
            for (int i = 0; i < 4; ++i)
                rgba.insert(rgba.end(), {220, 40, 40, alpha});
            return true;
        };
    }
};

bool wait_for(Artwork &artwork, ArtworkResult &result)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (artwork.take_result(result))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
} // namespace

TEST(AfterglowArtwork, ALoadedCoverGivesItsColours)
{
    Fixture fx;
    Artwork artwork(fx.fetch(), Fixture::decode(), fx.options());
    ASSERT_TRUE(artwork.start());
    const auto generation = artwork.request("http://10.0.0.2/cover.jpg");
    ArtworkResult result;
    ASSERT_TRUE(wait_for(artwork, result));
    EXPECT_EQ(result.generation, generation);
    EXPECT_TRUE(result.ok);
    EXPECT_GT(result.accent[0], 0.8f);
    EXPECT_LT(result.accent[1], 0.2f);
    EXPECT_EQ(result.width, 2);
    EXPECT_EQ(artwork.status(), "Artwork ready");
}

TEST(AfterglowArtwork, AFailedOrEmptyCoverIsNotOk)
{
    Fixture fx;
    Artwork artwork(fx.fetch(), Fixture::decode(), fx.options());
    ASSERT_TRUE(artwork.start());
    ArtworkResult result;
    artwork.request("http://10.0.0.2/missing.jpg");
    ASSERT_TRUE(wait_for(artwork, result));
    EXPECT_FALSE(result.ok) << "main keeps the neutral colours, not the default white";
    EXPECT_EQ(artwork.status(), "Artwork unavailable");
    artwork.request("http://10.0.0.2/clear.png");
    ASSERT_TRUE(wait_for(artwork, result));
    EXPECT_FALSE(result.ok) << "a wholly transparent picture has no colour";
    artwork.request("");
    ASSERT_TRUE(wait_for(artwork, result));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(artwork.status(), "No artwork");
}

TEST(AfterglowArtwork, AFailedCoverIsRetriedAFewTimesThenLeft)
{
    Fixture fx;
    auto options = fx.options();
    options.retry_us = 10000000;
    options.retries = 2;
    Artwork artwork(fx.fetch(), Fixture::decode(), options);
    ASSERT_TRUE(artwork.start());
    const std::string url = "http://10.0.0.2/missing.jpg";
    auto generation = artwork.request(url);
    ArtworkResult result;
    ASSERT_TRUE(wait_for(artwork, result));
    EXPECT_EQ(artwork.request(url), generation) << "not again straight away";
    for (int retry = 1; retry <= 2; ++retry)
    {
        fx.clock += 10000000;
        const auto next = artwork.request(url);
        EXPECT_GT(next, generation) << "retry " << retry;
        generation = next;
        ASSERT_TRUE(wait_for(artwork, result));
        EXPECT_EQ(result.generation, generation);
        EXPECT_FALSE(result.ok);
    }
    fx.clock += 10000000;
    EXPECT_EQ(artwork.request(url), generation) << "the retries are bounded";
    EXPECT_EQ(fx.fetches.load(), 3);
    EXPECT_GT(artwork.request("http://10.0.0.2/missing-too.jpg"), generation)
        << "another URL starts again";
}

TEST(AfterglowArtwork, ALoadedCoverIsNeverFetchedAgain)
{
    Fixture fx;
    Artwork artwork(fx.fetch(), Fixture::decode(), fx.options());
    ASSERT_TRUE(artwork.start());
    const auto generation = artwork.request("http://10.0.0.2/cover.jpg");
    ArtworkResult result;
    ASSERT_TRUE(wait_for(artwork, result));
    fx.clock += 60000000;
    EXPECT_EQ(artwork.request("http://10.0.0.2/cover.jpg"), generation);
    EXPECT_EQ(fx.fetches.load(), 1);
}
