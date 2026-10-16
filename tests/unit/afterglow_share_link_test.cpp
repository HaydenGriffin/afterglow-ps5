// Afterglow - Scan to save: link choice, the decoder and the QR module grid.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include "afterglow/share_link.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <string>

namespace
{
using namespace hui::afterglow;

ProviderLink link(const char *domain, const char *instance, const char *url)
{
    ProviderLink out;
    out.domain = domain;
    out.instance = instance;
    out.url = url;
    return out;
}

TEST(AfterglowShareLink, OnlyAPublicServicesOwnHttpsPageQualifies)
{
    EXPECT_TRUE(public_share_url("https://open.spotify.com/track/abc", "spotify.com"));
    EXPECT_TRUE(public_share_url("https://tidal.com/browse/track/1", "tidal.com"));
    EXPECT_TRUE(public_share_url("https://music.youtube.com/watch?v=abc", "youtube.com"));
    EXPECT_FALSE(public_share_url("http://open.spotify.com/track/abc", "spotify.com"));
    EXPECT_FALSE(public_share_url("https://192.168.1.4:8095/track/1", "spotify.com"));
    EXPECT_FALSE(public_share_url("https://open.spotify.com:8443/track/abc", "spotify.com"));
    EXPECT_FALSE(public_share_url("https://user:pw@open.spotify.com/track/abc", "spotify.com"));
    EXPECT_FALSE(public_share_url("https://evilspotify.com/track/abc", "spotify.com"));
    EXPECT_FALSE(public_share_url("https://open.spotify.com.evil.example/track", "spotify.com"));
    EXPECT_FALSE(
        public_share_url("https://music.youtube.com/watch?v=abc&access_token=x", "youtube.com"));
    EXPECT_FALSE(public_share_url("library://track/12", "spotify.com"));
    EXPECT_FALSE(
        public_share_url("https://open.spotify.com/" + std::string(300, 'a'), "spotify.com"));
}

TEST(AfterglowShareLink, TheServiceTheSongPlayedFromComesFirst)
{
    const std::vector<ProviderLink> links = {
        link("tidal", "tidal--a", "https://tidal.com/browse/track/1"),
        link("spotify", "spotify--b", "https://open.spotify.com/track/x")};
    const auto spotify = pick_share_link(links, "spotify--b");
    EXPECT_EQ(spotify.url, "https://open.spotify.com/track/x");
    EXPECT_EQ(spotify.service, "Spotify");
    EXPECT_EQ(pick_share_link(links, "tidal--a").service, "Tidal");
}

TEST(AfterglowShareLink, ThenTheSameServiceThenAStableOrder)
{
    std::vector<ProviderLink> links = {
        link("deezer", "deezer--z", "https://www.deezer.com/track/9"),
        link("tidal", "tidal--b", "https://tidal.com/browse/track/2"),
        link("tidal", "tidal--a", "https://tidal.com/browse/track/1")};
    EXPECT_EQ(pick_share_link(links, "tidal--gone").url, "https://tidal.com/browse/track/1");
    EXPECT_EQ(pick_share_link(links, "filesystem_local--x").url,
              "https://tidal.com/browse/track/1");
    std::reverse(links.begin(), links.end());
    EXPECT_EQ(pick_share_link(links, "").url, "https://tidal.com/browse/track/1");
}

TEST(AfterglowShareLink, NoPublicLinkMeansNothingToShow)
{
    EXPECT_TRUE(pick_share_link({}, "").url.empty());
    EXPECT_TRUE(
        pick_share_link({link("plex", "plex--a", "https://plex.example/library/1")}, "plex--a")
            .url.empty());
    EXPECT_TRUE(
        pick_share_link({link("spotify", "spotify--a", "http://192.168.0.9/track")}, "spotify--a")
            .url.empty());
}

// The decoder keeps only the chosen url and service; never a library uri.
TEST(AfterglowShareLink, TheQueueItemDecoderKeepsThePublicPage)
{
    const auto track = companion_wire::decode_track(
        R"({"queue_item_id":"q","streamdetails":{"provider":"spotify--b"},"media_item":{)"
        R"("name":"Song","uri":"library://track/7","provider":"library","media_type":"track",)"
        R"("provider_mappings":[)"
        R"({"provider_domain":"filesystem_local","provider_instance":"filesystem--a","url":null},)"
        R"({"provider_domain":"tidal","provider_instance":"tidal--a","url":"https://tidal.com/browse/track/1"},)"
        R"({"provider_domain":"spotify","provider_instance":"spotify--b","url":"https://open.spotify.com/track/x"}]}})");
    EXPECT_EQ(track.share_url, "https://open.spotify.com/track/x");
    EXPECT_EQ(track.share_service, "Spotify");
    const auto local = companion_wire::decode_track(
        R"({"media_item":{"name":"Song","uri":"library://track/7","provider_mappings":[)"
        R"({"provider_domain":"filesystem_local","provider_instance":"filesystem--a","url":null}]}})");
    EXPECT_TRUE(local.share_url.empty());
    EXPECT_TRUE(local.share_service.empty());
}

// The same url gives this grid in both apps (LG test/share-link.test.mjs).
TEST(AfterglowShareLink, QrEncodeGivesTheSharedModuleGrid)
{
    const char *expected[] = {
        "#######...#.#...#####.#######", "#.....#.#.#..###.#..#.#.....#",
        "#.###.#.....#.#....#..#.###.#", "#.###.#.###.######....#.###.#",
        "#.###.#..#...#..#####.#.###.#", "#.....#.##.##.###.....#.....#",
        "#######.#.#.#.#.#.#.#.#######", ".........###...##..##........",
        "#####.####.#####.###.#.#.#.#.", "##.###....#.##..####.##.#...#",
        "#.#..####.#..###.#..#.#......", ".##.#..##...#.###....###.#.#.",
        "##.#.######..#####.##....##..", ".#.....##..##.#.#..##.###...#",
        "..##..##..#.####.....#.####..", "..##.....#..#.#.#.###.###..#.",
        "##.####...#..###.#.......##..", "####.#..#..##...#..##.###.#.#",
        "#.#...#.###.#.#####.##..#.#..", "#..#...#.##.#..#..#..#..#..#.",
        "#.#.#######..###.#.######.###", "........#.###...#.#.#...#####",
        "#######.##..#.###.###.#.###..", "#.....#...#.#.#.#..##...#..#.",
        "#.###.#.##...##..#..#####.##.", "#.###.#.#.####....#....#.##.#",
        "#.###.#.#.#...######.#######.", "#.....#.####..#....##.##.#.#.",
        "#######.##.####..#.#..##..#..",
    };
    const auto grid = qr_grid("https://open.spotify.com/track/afterglowdemo0001");
    ASSERT_EQ(grid.size, 29);
    for (int y = 0; y < grid.size; ++y)
    {
        std::string row;
        for (int x = 0; x < grid.size; ++x)
            row += grid.at(x, y) ? '#' : '.';
        EXPECT_EQ(row, expected[y]) << "row " << y;
    }
    EXPECT_FALSE(grid.at(-1, 0));
    EXPECT_FALSE(grid.at(0, 29));
    EXPECT_EQ(qr_grid("").size, 0);
    EXPECT_EQ(qr_grid(std::string(400, 'a')).size, 0);
}
} // namespace
