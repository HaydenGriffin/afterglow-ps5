// Afterglow - Music Assistant library, queue window, grouping and stream-quality tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/companion.hpp"
#include <gtest/gtest.h>

using namespace hui::afterglow;

namespace
{
// Trimmed from a live Music Assistant 2.10.6 music/search reply (read-only capture).
constexpr const char *kSearch =
    R"({"artists":[{"item_id":"439","provider":"library","name":"Bonobo","uri":"library://artist/439","media_type":"artist","metadata":{"images":[{"type":"thumb","path":"https://i.scdn.co/image/ab6761610000e5eb37af35b51a7c9b689d86779a","proxy_id":"630ca953"}]},"favorite":true,"artist_type":"singer"}],
"albums":[{"item_id":"5m1RkwKeU7MV0Ni6PH2lPy","provider":"spotify--t7FHtqH8","name":"Black Sands","uri":"spotify--t7FHtqH8://album/5m1RkwKeU7MV0Ni6PH2lPy","media_type":"album","favorite":false,"year":2010,"artists":[{"name":"Bonobo","item_id":"0cmWgDlu9CwTgxPhf403hb","provider":"spotify--t7FHtqH8"}],"album_type":"album"},
 {"item_id":"2FMGW8WrG0BE0ZimHbHw2M","provider":"spotify--t7FHtqH8","name":"Distance in Static","uri":"spotify--t7FHtqH8://album/2FMGW8WrG0BE0ZimHbHw2M","media_type":"album","year":2026,"artists":[{"name":"Bonobo","item_id":"0cmWgDlu9CwTgxPhf403hb","provider":"spotify--t7FHtqH8"}]}],
"genres":[],
"tracks":[{"item_id":"2lJ4d8MCT6ZlDRHKJ1br14","provider":"spotify--t7FHtqH8","name":"Cirrus","uri":"spotify--t7FHtqH8://track/2lJ4d8MCT6ZlDRHKJ1br14","media_type":"track","duration":352,"artists":[{"name":"Bonobo","item_id":"0cmWgDlu9CwTgxPhf403hb","provider":"spotify--t7FHtqH8"}],"album":{"name":"The North Borders"}}],
"playlists":[{"item_id":"37i9dQZF1E4yLltmVk3nyb","provider":"spotify--t7FHtqH8","name":"Bonobo Radio","uri":"spotify--t7FHtqH8://playlist/37i9dQZF1E4yLltmVk3nyb","media_type":"playlist","owner":"Spotify"}],
"radio":[],
"audiobooks":[{"item_id":"1746Tinkj9h9sF4fazWeQg","provider":"spotify--t7FHtqH8","name":"Alan Partridge: Big Beacon","uri":"spotify--t7FHtqH8://audiobook/1746Tinkj9h9sF4fazWeQg","media_type":"audiobook","publisher":"Alan Partridge","authors":["Alan Partridge"],"duration":0}],
"podcasts":[{"item_id":"46PyAWl24SKfNomiMIkBwC","provider":"spotify--t7FHtqH8","name":"BONOBO","uri":"spotify--t7FHtqH8://podcast/46PyAWl24SKfNomiMIkBwC","media_type":"podcast","publisher":"Bigboiatallah ","total_episodes":8}],
"sound_effects":[]})";

// Trimmed from live music/podcasts/podcast_episodes and audiobooks/library_items replies.
constexpr const char *kEpisodes =
    R"([{"item_id":"1SZe7XVrKQnPrAIHxOG32h","provider":"spotify--t7FHtqH8","name":"BONOBO épisode 7","uri":"spotify--t7FHtqH8://podcast_episode/1SZe7XVrKQnPrAIHxOG32h","media_type":"podcast_episode","metadata":{"release_date":"2021-09-11T00:00:00+00:00"},"position":8,"podcast":{"name":"BONOBO"},"duration":1909},
{"item_id":"6G6yTDqGrEe0pKV7Im0tcX","provider":"spotify--t7FHtqH8","name":"BONOBO épisode 6","uri":"spotify--t7FHtqH8://podcast_episode/6G6yTDqGrEe0pKV7Im0tcX","media_type":"podcast_episode","metadata":{"release_date":"not a date"},"position":7,"duration":4000}])";
constexpr const char *kAudiobooks =
    R"([{"item_id":"5","provider":"library","name":"Daggermouth","uri":"library://audiobook/5","media_type":"audiobook","favorite":false,"authors":[{"name":"H. M. Wolfe"}],"narrators":[{"name":"Angel Pean"}],"duration":65514,"available":true}])";

// Trimmed from a live players/all reply.
constexpr const char *kPlayers =
    R"([{"player_id":"syncgroup_crdeqnda","name":"Home speakers","type":"group","available":true,"enabled":true,"hide_in_ui":false,"synced_to":null,"group_members":["upd1d78e6c","up88c876a0"],"can_group_with":["up2028bc229efe"]},
{"player_id":"up48b02d337816","name":"SHIELD","type":"player","available":true,"enabled":true,"hide_in_ui":false,"synced_to":null,"group_members":["up48b02d337816","up2028bc229efe"],"can_group_with":["up2028bc229efe","upd1d78e6c"]},
{"player_id":"up78d6dc12ac5b","name":"Office Clock","type":"player","available":false,"enabled":true,"hide_in_ui":false,"group_members":[],"can_group_with":[]},
{"player_id":"up2028bc229efe","name":"Living room TV","type":"player","available":true,"enabled":true,"hide_in_ui":false,"synced_to":"up48b02d337816","group_members":[],"can_group_with":["up48b02d337816"]}])";

std::string tracks(int count)
{
    std::string out = "[";
    for (int i = 0; i < count; ++i)
        out += (i ? "," : "") + std::string(R"({"uri":"library://track/)") + std::to_string(i) +
               R"(","name":"T)" + std::to_string(i) + R"(","media_type":"track"})";
    return out + "]";
}

CompanionTrack queue_item(const char *audio, const char *processing)
{
    return companion_wire::decode_track(
        std::string(
            R"({"queue_item_id":"q","media_item":{"uri":"library://track/1","name":"One"},"streamdetails":{"audio_format":)") +
        audio + R"(,"audio_processing":)" + processing + "}}");
}
} // namespace

TEST(AfterglowCompanionLibrary, SearchAsksForEveryMediaTypeAndFlattensInSectionOrder)
{
    BrowseRequest request;
    request.category = BrowseCategory::search;
    request.query = "  ";
    EXPECT_FALSE(companion_wire::browse_request(request).error.empty());
    request.query = " bonobo\" ";
    const auto wire = companion_wire::browse_request(request);
    EXPECT_EQ(wire.command, "music/search");
    Json args(wire.args);
    EXPECT_EQ(args.string(args.get(0, "search_query")), "bonobo\"");
    EXPECT_EQ(args.integer(args.get(0, "limit")), 20);
    for (const char *type :
         {"artist", "album", "track", "playlist", "radio", "podcast", "audiobook"})
        EXPECT_TRUE(args.has_string(args.get(0, "media_types"), type)) << type;
    request.query = std::string(300, 'x');
    EXPECT_FALSE(companion_wire::browse_request(request).error.empty());

    request.query = "bonobo";
    auto page = companion_wire::browse_page(request);
    companion_wire::decode_browse(kSearch, BrowseCategory::search, page);
    ASSERT_EQ(page.items.size(), 7U);
    const char *types[] = {"artist", "album", "album", "track", "playlist", "podcast", "audiobook"};
    for (std::size_t i = 0; i < page.items.size(); ++i)
        EXPECT_EQ(page.items[i].media_type, types[i]) << i;
    EXPECT_FALSE(page.more);
    EXPECT_EQ(page.items[1].subtitle, "Bonobo");
    EXPECT_EQ(page.items[3].subtitle, "Bonobo");
    EXPECT_EQ(page.items[3].album, "The North Borders");
    EXPECT_EQ(page.items[5].subtitle, "Bigboiatallah · 8 episodes");
    EXPECT_EQ(page.items[6].subtitle, "Alan Partridge");
    EXPECT_EQ(page.items[0].artwork, "/imageproxy/630ca953?size=512&fmt=png");
}

TEST(AfterglowCompanionLibrary, ArtistAndPodcastDrillDownsUseProviderIdentity)
{
    BrowseRequest request;
    request.category = BrowseCategory::artist_albums;
    EXPECT_FALSE(companion_wire::browse_request(request).error.empty());
    request.item_id = "426";
    auto wire = companion_wire::browse_request(request);
    EXPECT_EQ(wire.command, "music/artists/artist_albums");
    Json albums(wire.args);
    EXPECT_EQ(albums.string(albums.get(0, "item_id")), "426");
    EXPECT_EQ(albums.string(albums.get(0, "provider_instance_id_or_domain")), "library");
    request.category = BrowseCategory::artist_tracks;
    request.provider = "spotify--t7FHtqH8";
    wire = companion_wire::browse_request(request);
    EXPECT_EQ(wire.command, "music/artists/artist_tracks");
    EXPECT_EQ(Json(wire.args).string(Json(wire.args).get(0, "provider_instance_id_or_domain")),
              "spotify--t7FHtqH8");
    request.category = BrowseCategory::podcast_episodes;
    request.item_id.clear();
    EXPECT_FALSE(companion_wire::browse_request(request).error.empty());
    request.item_id = "46PyAWl24SKfNomiMIkBwC";
    wire = companion_wire::browse_request(request);
    EXPECT_EQ(wire.command, "music/podcasts/podcast_episodes");
    EXPECT_EQ(Json(wire.args).string(Json(wire.args).get(0, "item_id")), request.item_id);
}

TEST(AfterglowCompanionLibrary, EmptyLibraryArtistRetriesOnItsFirstAvailableProvider)
{
    BrowseRequest request;
    request.category = BrowseCategory::artist_albums;
    request.item_id = "426";
    request.provider = "library";
    const auto wire = companion_wire::artist_fallback(
        request,
        R"({"item_id":"426","provider":"library","name":"BICEP","provider_mappings":[{"item_id":"x","provider_instance":"gone--1","available":false},{"item_id":"73A3bLnfnz5BoQjb4gNCga","provider_instance":"spotify--t7FHtqH8","available":true}]})");
    EXPECT_EQ(wire.command, "music/artists/artist_albums");
    Json args(wire.args);
    EXPECT_EQ(args.string(args.get(0, "item_id")), "73A3bLnfnz5BoQjb4gNCga");
    EXPECT_EQ(args.string(args.get(0, "provider_instance_id_or_domain")), "spotify--t7FHtqH8");
    EXPECT_TRUE(
        companion_wire::artist_fallback(request, R"({"provider_mappings":[]})").command.empty());
    request.category = BrowseCategory::podcast_episodes;
    EXPECT_TRUE(companion_wire::artist_fallback(
                    request, R"({"provider_mappings":[{"item_id":"a","provider_instance":"b"}]})")
                    .command.empty());
}

TEST(AfterglowCompanionLibrary, LibraryListsPageOnTheServer)
{
    BrowseRequest request;
    request.category = BrowseCategory::podcasts;
    auto wire = companion_wire::browse_request(request);
    EXPECT_EQ(wire.command, "music/podcasts/library_items");
    request.category = BrowseCategory::audiobooks;
    request.offset = 120;
    wire = companion_wire::browse_request(request);
    EXPECT_EQ(wire.command, "music/audiobooks/library_items");
    Json args(wire.args);
    EXPECT_EQ(args.integer(args.get(0, "offset")), 120);
    EXPECT_EQ(args.integer(args.get(0, "limit")), 60);
    EXPECT_EQ(args.string(args.get(0, "order_by")), "sort_name");

    auto page = companion_wire::browse_page(request);
    EXPECT_TRUE(page.server_paged);
    companion_wire::decode_browse(tracks(60), request.category, page);
    EXPECT_EQ(page.items.size(), 60U);
    EXPECT_TRUE(page.more);
    page = companion_wire::browse_page(request);
    companion_wire::decode_browse(kAudiobooks, request.category, page);
    ASSERT_EQ(page.items.size(), 1U);
    EXPECT_FALSE(page.more);
    EXPECT_EQ(page.items[0].subtitle, "H. M. Wolfe");
}

TEST(AfterglowCompanionLibrary, OffsetsAreBounded)
{
    BrowseRequest request;
    request.category = BrowseCategory::albums;
    request.offset = 1 << 30;
    EXPECT_FALSE(companion_wire::browse_request(request).error.empty());
    request.offset = -5;
    const auto negative = companion_wire::browse_request(request).args;
    EXPECT_EQ(Json(negative).integer(Json(negative).get(0, "offset")), 0);
}

TEST(AfterglowCompanionLibrary, WholeListsAreSkippedAndCutHereAcrossPartialChunks)
{
    BrowseRequest request;
    request.category = BrowseCategory::tracks;
    request.media_type = "album";
    request.item_id = "7";
    request.offset = 200;
    auto page = companion_wire::browse_page(request);
    EXPECT_FALSE(page.server_paged);
    // Music Assistant streams long lists as partial chunks: 250 then 200 items.
    companion_wire::decode_browse(tracks(250), request.category, page);
    companion_wire::decode_browse(tracks(200), request.category, page);
    ASSERT_EQ(page.items.size(), 200U);
    EXPECT_EQ(page.items.front().title, "T200");
    EXPECT_EQ(page.items[50].title, "T0");
    EXPECT_TRUE(page.more);
    page = companion_wire::browse_page(request);
    companion_wire::decode_browse(tracks(400), request.category, page);
    EXPECT_EQ(page.items.size(), 200U);
    EXPECT_FALSE(page.more);

    request.category = BrowseCategory::recent;
    request.offset = 30;
    const auto args = companion_wire::browse_request(request).args;
    EXPECT_EQ(Json(args).integer(Json(args).get(0, "limit")), 61);
    page = companion_wire::browse_page(request);
    companion_wire::decode_browse(tracks(45), request.category, page);
    EXPECT_EQ(page.items.size(), 15U);
    EXPECT_EQ(page.items.front().title, "T30");
    EXPECT_FALSE(page.more);
}

TEST(AfterglowCompanionLibrary, EpisodesShowDateAndLength)
{
    BrowseRequest request;
    request.category = BrowseCategory::podcast_episodes;
    request.item_id = "p";
    auto page = companion_wire::browse_page(request);
    companion_wire::decode_browse(kEpisodes, request.category, page);
    ASSERT_EQ(page.items.size(), 2U);
    EXPECT_EQ(page.items[0].media_type, "podcast_episode");
    EXPECT_EQ(page.items[0].subtitle, "11 Sep 2021 · 32 min");
    EXPECT_EQ(page.items[1].subtitle, "1 h 7 min");
    EXPECT_EQ(page.items[0].duration, 1909);
}

TEST(AfterglowCompanionLibrary, LoadMoreStopsAtTheOffsetCap)
{
    BrowseRequest request;
    request.category = BrowseCategory::albums;
    request.offset = companion_wire::kMaxBrowseOffset;
    const auto at_cap = companion_wire::browse_request(request);
    EXPECT_TRUE(at_cap.error.empty());
    EXPECT_EQ(Json(at_cap.args).integer(Json(at_cap.args).get(0, "offset")), 2000);
    request.offset = companion_wire::kMaxBrowseOffset + 60;
    EXPECT_FALSE(companion_wire::browse_request(request).error.empty())
        << "past the cap is refused, not clamped back to a page already shown";
    EXPECT_TRUE(companion_wire::browse_has_more(true, 1940));
    EXPECT_FALSE(companion_wire::browse_has_more(true, 2000));
    EXPECT_FALSE(companion_wire::browse_has_more(true, 2040));
    EXPECT_FALSE(companion_wire::browse_has_more(false, 60));
    // A refused page never starts loading.
    Companion companion;
    companion.browse(request);
    EXPECT_FALSE(companion.snapshot().browse_loading);
    EXPECT_FALSE(companion.snapshot().browse_has_more);
}

TEST(AfterglowCompanionLibrary, QueueWindowRequestsFiftyItemsFromTheOffset)
{
    const auto wire = companion_wire::queue_items_request("room\"q", 48);
    EXPECT_EQ(wire.command, "player_queues/items");
    Json args(wire.args);
    EXPECT_EQ(args.string(args.get(0, "queue_id")), "room\"q");
    EXPECT_EQ(args.integer(args.get(0, "limit")), 50);
    EXPECT_EQ(args.integer(args.get(0, "offset")), 48);
    const auto negative = companion_wire::queue_items_request("q", -3).args;
    EXPECT_EQ(Json(negative).integer(Json(negative).get(0, "offset")), 0);
    // Without a connection the window has nothing to clamp against; it must not crash.
    Companion companion;
    companion.set_queue_offset(10);
    companion.set_queue_offset(-1);
    companion.set_queue_offset(-2); // the Queue sheet's "back to default"
    EXPECT_EQ(companion.snapshot().queue_offset, 0);
}

TEST(AfterglowCompanionLibrary, PlayersCarryGroupFieldsAndSkipGroupsAndUnavailable)
{
    const auto players = companion_wire::decode_players(kPlayers);
    ASSERT_EQ(players.size(), 2U);
    EXPECT_EQ(players[0].name, "Living room TV");
    EXPECT_EQ(players[0].synced_to, "up48b02d337816");
    EXPECT_EQ(players[0].type, "player");
    EXPECT_TRUE(players[0].available);
    EXPECT_EQ(players[1].id, "up48b02d337816");
    ASSERT_EQ(players[1].group_members.size(), 2U);
    EXPECT_EQ(players[1].group_members[1], "up2028bc229efe");
    ASSERT_EQ(players[1].can_group_with.size(), 2U);
    EXPECT_EQ(players[1].can_group_with[0], "up2028bc229efe");
    EXPECT_TRUE(players[1].synced_to.empty());
    EXPECT_TRUE(companion_wire::decode_players("{}").empty());
}

TEST(AfterglowCompanionLibrary, GroupEditsUseSetMembersWithoutAPlayingQueue)
{
    CompanionSnapshot target;
    target.connected = true;
    CompanionAction action;
    action.kind = CompanionActionKind::group_add;
    action.option = "up48b02d337816";
    action.text = "up2028bc229efe";
    auto wire = companion_wire::action_request(action, target);
    EXPECT_TRUE(wire.error.empty());
    EXPECT_EQ(wire.command, "players/cmd/set_members");
    {
        Json args(wire.args);
        EXPECT_EQ(args.string(args.get(0, "target_player")), "up48b02d337816");
        EXPECT_TRUE(args.has_string(args.get(0, "player_ids_to_add"), "up2028bc229efe"));
        EXPECT_EQ(args.get(0, "player_ids_to_remove"), -1);
    }
    action.kind = CompanionActionKind::group_remove;
    wire = companion_wire::action_request(action, target);
    {
        Json args(wire.args);
        EXPECT_TRUE(args.has_string(args.get(0, "player_ids_to_remove"), "up2028bc229efe"));
        EXPECT_EQ(args.get(0, "player_ids_to_add"), -1);
    }
    action.text = action.option;
    EXPECT_FALSE(companion_wire::action_request(action, target).error.empty());
    action.text.clear();
    EXPECT_FALSE(companion_wire::action_request(action, target).error.empty());
    action.text = std::string(300, 'a');
    EXPECT_FALSE(companion_wire::action_request(action, target).error.empty());
    action.text = "up2028bc229efe";
    target.connected = false;
    EXPECT_FALSE(companion_wire::action_request(action, target).error.empty());
}

TEST(AfterglowCompanionLibrary, StreamQualityMatchesTheLgBadge)
{
    // The LG app's own badge cases (test/stream-details.test.mjs).
    auto track =
        queue_item(R"({"content_type":"flac","sample_rate":44100,"bit_depth":16,"bit_rate":0})",
                   R"({"input_fidelity":{"quality":"lossless"}})");
    EXPECT_EQ(track.quality, "Lossless");
    EXPECT_EQ(track.quality_badge, "Lossless · FLAC 44.1 kHz / 16-bit");
    EXPECT_EQ(track.content_type, "flac");
    EXPECT_EQ(track.sample_rate, 44100);
    EXPECT_EQ(track.bit_depth, 16);
    EXPECT_EQ(track.format, "flac / 44100 Hz / 16 bit");
    track = queue_item(R"({"content_type":"flac","sample_rate":96000,"bit_depth":24,"bit_rate":0})",
                       R"({"input_fidelity":{"quality":"hi_res"}})");
    EXPECT_EQ(track.quality_badge, "Hi-res · FLAC 96 kHz / 24-bit");
    track =
        queue_item(R"({"content_type":"ogg","sample_rate":44100,"bit_depth":16,"bit_rate":320})",
                   R"({"input_fidelity":{"quality":"lossy"}})");
    EXPECT_EQ(track.quality_badge, "Compressed · OGG 44.1 kHz / 320 kbps");
    EXPECT_EQ(track.bit_rate, 320);
    // Music Assistant 2.10 itself grades lossy input "standard".
    track =
        queue_item(R"({"content_type":"ogg","sample_rate":44100,"bit_depth":16,"bit_rate":320})",
                   R"({"input_fidelity":{"quality":"standard"}})");
    EXPECT_EQ(track.quality_badge, "Compressed · OGG 44.1 kHz / 320 kbps");
    track = queue_item(R"({"content_type":"mp3","sample_rate":22050,"bit_depth":0,"bit_rate":64})",
                       R"({"input_fidelity":{"quality":"low"}})");
    EXPECT_EQ(track.quality_badge, "Low quality · MP3 22.1 kHz / 64 kbps");
    // A live idle queue item: audio_processing is null, so no grade and no bit depth.
    track = queue_item(
        R"({"content_type":"flac","codec_type":"flac","sample_rate":44100,"bit_depth":24,"channels":2,"output_format_str":"flac","bit_rate":0})",
        "null");
    EXPECT_TRUE(track.quality.empty());
    EXPECT_EQ(track.quality_badge, "FLAC 44.1 kHz");
    track = queue_item(R"({"content_type":"?","sample_rate":0})",
                       R"({"input_fidelity":{"quality":"unknown"}})");
    EXPECT_TRUE(track.quality_badge.empty());
    EXPECT_TRUE(companion_wire::decode_track(R"({"name":"x"})").quality_badge.empty());
}
