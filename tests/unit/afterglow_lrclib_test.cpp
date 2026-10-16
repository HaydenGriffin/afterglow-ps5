// Afterglow - LRCLIB matching, request and answer tests (LG test/lrclib.test.mjs cases).
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/http_response.hpp"
#include "afterglow/lrclib.hpp"
#include "afterglow/version.hpp"
#include <gtest/gtest.h>

namespace
{
using namespace hui::afterglow;
using lrclib::Query;
using lrclib::Record;

Record hit()
{
    Record r;
    r.track = "That's It (feat. Future & Metro Boomin)";
    r.artist = "Yung Lean";
    r.duration = 162;
    r.synced = "[00:18.88]Tryna put it all";
    r.plain = "Tryna put it all";
    return r;
}
Query want(double duration = 161)
{
    Query q;
    q.title = "That's It";
    q.artist = "Yung Lean";
    q.duration = duration;
    return q;
}

TEST(AfterglowLrclib, TitlesCompareWithoutFeaturedArtistsVersionsOrCurlyQuotes)
{
    EXPECT_EQ(lrclib::base_title("That\xE2\x80\x99s It (feat. Future) [from GTA VI]"), "that's it");
    EXPECT_EQ(lrclib::base_title("Song - 2011 Remaster"), "song");
    EXPECT_EQ(lrclib::base_title("  Song  [Live]"), "song");
    EXPECT_EQ(lrclib::base_title("Re-Run - Live"), "re-run") << "a dash inside a word stays";
    EXPECT_EQ(lrclib::base_title("Song - (Live)"), "song -")
        << "as LG: the bracket goes first, with the space before it";
    EXPECT_EQ(lrclib::base_title("(Intro)"), "");
    EXPECT_EQ(lrclib::lower("\xC5\x81\xC3\x93\xC5\xB9 ABC \xD0\x96"),
              "\xC5\x82\xC3\xB3\xC5\xBA abc \xD0\xB6")
        << "Polish and Cyrillic capitals lower as in JavaScript";
}

TEST(AfterglowLrclib, TheHitMatchesTitleArtistAndLengthTimedLyricsFirst)
{
    auto plain_only = hit();
    plain_only.synced.clear();
    plain_only.duration = 160;
    auto found = lrclib::pick({plain_only, hit()}, want());
    ASSERT_TRUE(found);
    EXPECT_EQ(found->plain, "Tryna put it all");
    EXPECT_EQ(found->lrc, "[00:18.88]Tryna put it all");

    auto other_cut = hit();
    other_cut.duration = 184;
    EXPECT_FALSE(lrclib::pick({other_cut}, want())) << "a different cut is not used";
    auto other_artist = hit();
    other_artist.artist = "Someone Else";
    EXPECT_FALSE(lrclib::pick({other_artist}, want()));
    auto other_title = hit();
    other_title.track = "That's All";
    EXPECT_FALSE(lrclib::pick({other_title}, want()));
    auto instrumental = hit();
    instrumental.instrumental = true;
    EXPECT_FALSE(lrclib::pick({instrumental}, want()));
    auto empty = hit();
    empty.synced.clear();
    empty.plain.clear();
    EXPECT_FALSE(lrclib::pick({empty}, want()));
    auto credited = hit();
    credited.artist = "Yung Lean & Bladee";
    EXPECT_TRUE(lrclib::pick({credited}, want())) << "the artist is named among others";
    EXPECT_TRUE(lrclib::pick({other_cut}, want(0))) << "no length known: any length";
}

TEST(AfterglowLrclib, MatchingAlbumAndNearestDurationSelectTheTimedRecording)
{
    Query q = want(162);
    q.album = "Album";
    auto other = hit();
    other.album = "Compilation";
    other.synced = "other";
    auto near = hit();
    near.album = "Album";
    near.duration = 164;
    near.synced = "near";
    auto exact = hit();
    exact.album = "Album";
    exact.synced = "exact";
    EXPECT_EQ(lrclib::pick({other, near, exact}, q)->lrc, "exact");
    EXPECT_EQ(lrclib::pick({other, near}, q)->lrc, "near");
    q.album.clear();
    EXPECT_EQ(lrclib::pick({near, exact}, q)->lrc, "exact");
}

TEST(AfterglowLrclib, TheSearchAsksForTheBaseTitleAndArtist)
{
    Query q;
    q.title = "That's It (feat. Future)";
    q.artist = "Yung Lean";
    EXPECT_EQ(lrclib::search_path(q), "/api/search?track_name=that's%20it&artist_name=Yung%20Lean");
    q.artist.clear();
    q.title = "Café & Co / \xC5\x81\xC3\xB3"
              "d\xC5\xBA";
    EXPECT_EQ(lrclib::search_path(q),
              "/api/search?track_name=caf%C3%A9%20%26%20co%20%2F%20%C5%82%C3%B3d%C5%BA");
    q.title = "(Untitled)";
    EXPECT_EQ(lrclib::search_path(q), "") << "nothing to search for";
    EXPECT_EQ(lrclib::user_agent(), std::string("Afterglow PS5/") + kVersion +
                                        " (https://github.com/HaydenGriffin/afterglow-ps5)");
}

TEST(AfterglowLrclib, ReadsTheSearchAnswer)
{
    std::vector<Record> records;
    const std::string answer =
        R"j([{"id":38770140,"name":"That's It","trackName":"That's It (feat. Future \u0026 Metro Boomin)",)j"
        R"("artistName":"Yung Lean","albumName":"Album","duration":162.0,"instrumental":false,)"
        R"("plainLyrics":"Line one\nLine \"two\" \ud83c\udfb5","syncedLyrics":null,"extra":{"a":[1,2,{"b":null}]}},)"
        R"( {"trackName":"Other","artistName":null,"duration":1.5e2,"instrumental":true}])";
    ASSERT_TRUE(lrclib::parse_results(answer, records));
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].track, "That's It (feat. Future & Metro Boomin)");
    EXPECT_EQ(records[0].artist, "Yung Lean");
    EXPECT_EQ(records[0].album, "Album");
    EXPECT_DOUBLE_EQ(records[0].duration, 162);
    EXPECT_FALSE(records[0].instrumental);
    EXPECT_EQ(records[0].plain, "Line one\nLine \"two\" \xF0\x9F\x8E\xB5");
    EXPECT_EQ(records[0].synced, "");
    EXPECT_EQ(records[1].artist, "");
    EXPECT_DOUBLE_EQ(records[1].duration, 150);
    EXPECT_TRUE(records[1].instrumental);
    EXPECT_TRUE(lrclib::parse_results(" [ ] ", records));
    EXPECT_TRUE(records.empty());
}

TEST(AfterglowLrclib, ABrokenOrCutOffAnswerIsAFailureNotAMiss)
{
    std::vector<Record> records;
    EXPECT_FALSE(lrclib::parse_results("", records));
    EXPECT_FALSE(lrclib::parse_results("{\"error\":\"x\"}", records));
    EXPECT_FALSE(lrclib::parse_results("[{\"trackName\":\"cut", records));
    EXPECT_FALSE(lrclib::parse_results("[{\"trackName\":\"a\"}] trailing", records));
    EXPECT_FALSE(lrclib::parse_results("[{\"trackName\":\"bad \\x escape\"}]", records));
    EXPECT_FALSE(lrclib::parse_results(std::string(100, '[') + std::string(100, ']'), records))
        << "nesting is bounded";
    std::string many = "[";
    for (int i = 0; i < 150; ++i)
        many += std::string(i ? "," : "") + "{\"trackName\":\"t\"}";
    ASSERT_TRUE(lrclib::parse_results(many + "]", records));
    EXPECT_EQ(records.size(), lrclib::kMaxRecords);
}

TEST(AfterglowHttpResponse, FramesByLengthOrChunksWithoutWaitingForClose)
{
    using State = http::Response::State;
    const std::string head = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
    auto r = http::parse_response(head + "Content-Length: 4\r\n\r\n[]", false, 100);
    EXPECT_EQ(r.state, State::incomplete);
    r = http::parse_response(head + "content-length: 4\r\n\r\n[ ]\n", false, 100);
    EXPECT_EQ(r.state, State::complete);
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "[ ]\n");
    r = http::parse_response(head + "Content-Length: 4\r\n\r\n[]", true, 100);
    EXPECT_EQ(r.state, State::malformed) << "cut short";

    const std::string chunked = head + "Transfer-Encoding: chunked\r\n\r\n";
    r = http::parse_response(chunked + "2\r\n[]\r\n", false, 100);
    EXPECT_EQ(r.state, State::incomplete);
    r = http::parse_response(chunked + "2;x=y\r\n[ \r\n1\r\n]\r\n0\r\n\r\n", false, 100);
    EXPECT_EQ(r.state, State::complete);
    EXPECT_EQ(r.body, "[ ]");
    r = http::parse_response(chunked + "zz\r\n", false, 100);
    EXPECT_EQ(r.state, State::malformed);

    r = http::parse_response(head + "\r\n[]", false, 100);
    EXPECT_EQ(r.state, State::incomplete) << "no framing: the body runs to the close";
    r = http::parse_response(head + "\r\n[]", true, 100);
    EXPECT_EQ(r.state, State::complete);
    EXPECT_EQ(r.body, "[]");
}

TEST(AfterglowHttpResponse, CapsTheWireNotJustTheBody)
{
    using State = http::Response::State;
    const std::size_t max_body = 65536;
    const std::string chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    // A body within its limit, sent one byte per chunk: six wire bytes for each.
    std::string tiny = chunked;
    for (std::size_t i = 0; i < max_body; ++i)
        tiny += "1\r\nx\r\n";
    ASSERT_GT(tiny.size(), http::max_wire(max_body));
    EXPECT_EQ(http::parse_response(tiny, false, max_body).state, State::too_large);
    EXPECT_EQ(http::parse_response(tiny + "0\r\n\r\n", false, max_body).state, State::too_large);
    // The same body in a few ordinary chunks is fine.
    std::string normal = chunked;
    for (int i = 0; i < 64; ++i)
        normal += "400\r\n" + std::string(1024, 'x') + "\r\n";
    const auto r = http::parse_response(normal + "0\r\n\r\n", false, max_body);
    EXPECT_EQ(r.state, State::complete);
    EXPECT_EQ(r.body.size(), max_body);
    // Framing that never ends: a chunk-size line padded with extensions.
    const auto padded = chunked + "1;" + std::string(http::max_wire(max_body), 'x') + "\r\n";
    EXPECT_EQ(http::parse_response(padded, false, max_body).state, State::too_large);
    // A complete response is not refused for bytes after its Content-Length.
    const std::string sized = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n[]";
    EXPECT_EQ(
        http::parse_response(sized + std::string(http::max_wire(max_body), ' '), false, max_body)
            .state,
        State::complete);
    const auto partial = http::parse_response(sized.substr(0, sized.size() - 1), false, max_body);
    EXPECT_EQ(partial.state, State::incomplete);
    EXPECT_EQ(partial.complete_at, sized.size());
    EXPECT_EQ(http::max_wire(static_cast<std::size_t>(-1)), static_cast<std::size_t>(-1));
}

TEST(AfterglowHttpResponse, StatusLimitsAndTheRequest)
{
    using State = http::Response::State;
    auto r = http::parse_response("HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\n\r\n",
                                  false, 100);
    EXPECT_EQ(r.state, State::complete);
    EXPECT_EQ(r.status, 429);
    EXPECT_EQ(
        http::parse_response("HTTP/1.1 200 OK\r\nContent-Length: 101\r\n\r\n", false, 100).state,
        State::too_large);
    EXPECT_EQ(http::parse_response("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n65\r\n",
                                   false, 100)
                  .state,
              State::too_large);
    EXPECT_EQ(http::parse_response("SSH-2.0\r\n\r\n", false, 100).state, State::malformed);
    EXPECT_EQ(http::parse_response(std::string(http::kMaxHeader + 1, 'x'), false, 100).state,
              State::malformed);
    EXPECT_EQ(http::get_request("lrclib.net", "/api/search?track_name=a", "Agent/1"),
              "GET /api/search?track_name=a HTTP/1.1\r\nHost: lrclib.net\r\nUser-Agent: Agent/1\r\n"
              "Accept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
}
} // namespace
