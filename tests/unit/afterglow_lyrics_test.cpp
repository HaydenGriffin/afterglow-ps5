// Afterglow - Synchronized lyric parsing and seek regression tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/lyrics.hpp"
#include <gtest/gtest.h>

using hui::afterglow::current_lyric;
using hui::afterglow::current_word;
using hui::afterglow::parse_lyrics;
using hui::afterglow::sung_bytes;
using hui::afterglow::sung_per_row;

TEST(AfterglowLyrics, AppliesSignedOffsetAndStripsOnlyWordTimestamps)
{
    // A positive [offset:] brings the lyrics sooner, as the LRC format defines.
    const auto lines = parse_lyrics("[offset:+500]\n[00:01]A <00:01.20>line <live>\n", true);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_DOUBLE_EQ(lines[0].seconds, 0.5);
    EXPECT_EQ(lines[0].text, "A line <live>");
    EXPECT_DOUBLE_EQ(parse_lyrics("[offset:-250]\n[00:01]One", true)[0].seconds, 1.25);
    EXPECT_DOUBLE_EQ(parse_lyrics("[offset:250]\n[00:01]One", true)[0].seconds, 0.75);
    EXPECT_DOUBLE_EQ(parse_lyrics("[offset:+5000]\n[00:01]One", true)[0].seconds, 0)
        << "never before the start";
}

TEST(AfterglowLyrics, ExpandsRepeatedTimestampsAndSortsStably)
{
    const auto lines = parse_lyrics("[01:02.50][00:03.25]Repeated\r\n"
                                    "[00:03.25]Same instant\n[00:01]First\n",
                                    true);
    ASSERT_EQ(lines.size(), 4U);
    EXPECT_DOUBLE_EQ(lines[0].seconds, 1);
    EXPECT_EQ(lines[0].text, "First");
    EXPECT_DOUBLE_EQ(lines[1].seconds, 3.25);
    EXPECT_EQ(lines[1].text, "Repeated");
    EXPECT_DOUBLE_EQ(lines[2].seconds, 3.25);
    EXPECT_EQ(lines[2].text, "Same instant");
    EXPECT_DOUBLE_EQ(lines[3].seconds, 62.5);
    EXPECT_EQ(lines[3].text, "Repeated");
}

TEST(AfterglowLyrics, RejectsMalformedNonfiniteAndOutOfRangeTimestamps)
{
    const auto lines = parse_lyrics("[ar:Artist]\n[00:nan]NaN\n[00:inf]Infinity\n"
                                    "[00:60]Bad seconds\n[00:-1]Negative\n[-1:10]Negative minute\n"
                                    "[10001:00]Too far\n[00:03oops]Trailing junk\n"
                                    "[00:02]Valid\n",
                                    true);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_DOUBLE_EQ(lines[0].seconds, 2);
    EXPECT_EQ(lines[0].text, "Valid");
}

TEST(AfterglowLyrics, ActiveLineTracksBackwardSeekAndExactBoundary)
{
    const auto lines = parse_lyrics("[00:02]One\n[00:10]Two\n[00:30]Three", true);
    EXPECT_EQ(current_lyric(lines, 1.99), -1);
    EXPECT_EQ(current_lyric(lines, 2), 0);
    EXPECT_EQ(current_lyric(lines, 10), 1);
    EXPECT_EQ(current_lyric(lines, 40), 2);
    EXPECT_EQ(current_lyric(lines, 5), 0);
    EXPECT_EQ(current_lyric(lines, -1), -1);
    EXPECT_EQ(current_lyric({}, 20), -1);
}

TEST(AfterglowLyrics, PlainLyricsPreserveBracketedWordsAndBoundLength)
{
    const auto plain = parse_lyrics("[Chorus]\r\nSing along\r\n\n", false);
    ASSERT_EQ(plain.size(), 2U);
    EXPECT_EQ(plain[0].text, "[Chorus]");
    EXPECT_EQ(plain[1].text, "Sing along");
    EXPECT_EQ(current_lyric(plain, 100), -1);
    std::string many;
    for (int i = 0; i < 600; ++i)
        many += "[00:01]Line\n";
    EXPECT_EQ(parse_lyrics(many, true).size(), 500U);
    const auto large = parse_lyrics(std::string(70000, 'x'), false);
    ASSERT_EQ(large.size(), 1U);
    EXPECT_EQ(large[0].text.size(), 65536U);
}

TEST(AfterglowLyrics, EnhancedTagsBecomeWordTimingsOverTheCleanText)
{
    const auto lines = parse_lyrics(
        "[offset:-500]\n[00:10.00]<00:10.00>Hold <00:10.50>on  <00:11.25>tight\n", true);
    ASSERT_EQ(lines.size(), 1U);
    const auto &line = lines[0];
    EXPECT_EQ(line.text, "Hold on  tight");
    ASSERT_EQ(line.words.size(), 3U);
    EXPECT_DOUBLE_EQ(line.words[0].seconds, 10.5) << "the file offset moves words too";
    EXPECT_DOUBLE_EQ(line.words[2].seconds, 11.75);
    EXPECT_EQ(line.text.substr(line.words[1].begin, line.words[1].end - line.words[1].begin), "on");
    EXPECT_EQ(line.text.substr(line.words[2].begin), "tight");
    EXPECT_EQ(current_word(line, 10.4), -1);
    EXPECT_EQ(current_word(line, 10.5), 0);
    EXPECT_EQ(current_word(line, 11.0), 1);
    EXPECT_EQ(current_word(line, 99), 2);
    EXPECT_EQ(sung_bytes(line, 10.4), 0U);
    EXPECT_EQ(sung_bytes(line, 11.0), 7U) << "through the end of \"on\"";
    EXPECT_EQ(sung_bytes(line, 12), line.text.size());
}

TEST(AfterglowLyrics, LinesWithoutWordTimingsStayLineLevel)
{
    const auto lines = parse_lyrics("[00:01]Plain line\n", true);
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_TRUE(lines[0].words.empty());
    EXPECT_EQ(current_word(lines[0], 5), -1);
    EXPECT_EQ(sung_bytes(lines[0], 5), std::string::npos);
}

TEST(AfterglowLyrics, RepeatedLinesShiftTheirWordTimingsAndWordsAreBounded)
{
    const auto lines = parse_lyrics("[00:10][01:10]<00:10>La <00:11>la\n", true);
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_DOUBLE_EQ(lines[1].words[0].seconds, 70);
    EXPECT_DOUBLE_EQ(lines[1].words[1].seconds, 71);
    std::string many = "[00:01]";
    for (int i = 0; i < 80; ++i)
        many += "<00:01." + std::to_string(10 + i) + ">w ";
    const auto bounded = parse_lyrics(many, true);
    ASSERT_EQ(bounded.size(), 1U);
    EXPECT_EQ(bounded[0].words.size(), hui::afterglow::kMaxLyricWords);
    EXPECT_EQ(bounded[0].text.find('<'), std::string::npos);
}

TEST(AfterglowLyrics, SungBytesSplitAcrossWrappedRows)
{
    const std::string text = "Hold on tight to me";
    const std::vector<std::string> rows = {"Hold on", "tight to me"};
    EXPECT_EQ(sung_per_row(rows, text, 0), (std::vector<std::size_t>{0, 0}));
    EXPECT_EQ(sung_per_row(rows, text, 4), (std::vector<std::size_t>{4, 0}));
    EXPECT_EQ(sung_per_row(rows, text, 13), (std::vector<std::size_t>{7, 5}));
    EXPECT_EQ(sung_per_row(rows, text, text.size()), (std::vector<std::size_t>{7, 11}));
    const std::vector<std::string> cut = {"Hold on", "tight \xE2\x80\xA6"};
    EXPECT_EQ(sung_per_row(cut, text, 13), (std::vector<std::size_t>{7, 5}));
    EXPECT_EQ(sung_per_row(cut, text, text.size())[1], cut[1].size());
    EXPECT_EQ(sung_per_row({"unrelated"}, text, 3), (std::vector<std::size_t>{0}));
}

TEST(AfterglowLyrics, TimingLabelsAndStepsFollowLg)
{
    using hui::afterglow::lyrics_timing_label;
    using hui::afterglow::step_lyrics_offset;
    EXPECT_EQ(lyrics_timing_label(0), "With the music");
    EXPECT_EQ(lyrics_timing_label(300), "0.3 s later");
    EXPECT_EQ(lyrics_timing_label(-1200), "1.2 s earlier");
    EXPECT_EQ(lyrics_timing_label(10000), "10.0 s later");
    EXPECT_EQ(step_lyrics_offset(0, 1), 100);
    EXPECT_EQ(step_lyrics_offset(0, -1), -100);
    EXPECT_EQ(step_lyrics_offset(9950, 1), 10000);
    EXPECT_EQ(step_lyrics_offset(10000, 1), 10000);
    EXPECT_EQ(step_lyrics_offset(-10000, -1), -10000);
    EXPECT_EQ(step_lyrics_offset(50, -1), 0) << "an old 50 ms value meets zero";
    EXPECT_EQ(step_lyrics_offset(-50, -1), -100);
    EXPECT_EQ(step_lyrics_offset(150, 1), 200);
}
