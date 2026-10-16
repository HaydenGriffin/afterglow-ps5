// Afterglow - Dev request parsing, the BMP writer and the status document.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/dev_requests.hpp"
#include "afterglow/protocol.hpp"
#include <gtest/gtest.h>

using hui::afterglow::DevRequest;
using hui::afterglow::parse_dev_request;

namespace
{
constexpr const char *kToken = "abcdefgh12345678";
}

TEST(AfterglowDevRequests, TokensMustBeLongAndPlain)
{
    EXPECT_TRUE(hui::afterglow::valid_dev_token(kToken));
    EXPECT_TRUE(hui::afterglow::valid_dev_token("a.b-c_d0123456789"));
    EXPECT_FALSE(hui::afterglow::valid_dev_token("short-token"));
    EXPECT_FALSE(hui::afterglow::valid_dev_token("abcdefgh 2345678"));
    EXPECT_FALSE(hui::afterglow::valid_dev_token(std::string(129, 'a')));
}

TEST(AfterglowDevRequests, ParsesEachVerb)
{
    const auto quit = parse_dev_request("quit - abcdefgh12345678 r1\n", kToken);
    ASSERT_TRUE(quit);
    EXPECT_EQ(quit->verb, DevRequest::Verb::quit);
    EXPECT_EQ(quit->id, "r1");
    const auto status = parse_dev_request("status - abcdefgh12345678 r-2\r\n", kToken);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->verb, DevRequest::Verb::status);
    const auto shot = parse_dev_request("screenshot now_playing abcdefgh12345678 r_3", kToken);
    ASSERT_TRUE(shot);
    EXPECT_EQ(shot->verb, DevRequest::Verb::screenshot);
    EXPECT_EQ(shot->argument, "now_playing");
}

TEST(AfterglowDevRequests, PressNamesAButton)
{
    const auto press = parse_dev_request("press triangle abcdefgh12345678 r4", kToken);
    ASSERT_TRUE(press);
    EXPECT_EQ(press->verb, DevRequest::Verb::press);
    EXPECT_EQ(press->argument, "triangle");
    EXPECT_EQ(hui::afterglow::dev_press_action("cross"), hui::Action::confirm);
    EXPECT_EQ(hui::afterglow::dev_press_action("circle"), hui::Action::back);
    EXPECT_EQ(hui::afterglow::dev_press_action("r2"), hui::Action::jump_next);
    EXPECT_EQ(hui::afterglow::dev_press_action("options"), hui::Action::menu);
    EXPECT_FALSE(hui::afterglow::dev_press_action("ps"));
    EXPECT_FALSE(hui::afterglow::dev_press_action("Cross"));
    EXPECT_FALSE(parse_dev_request("press ps abcdefgh12345678 r4", kToken)) << "no PS button";
    EXPECT_FALSE(parse_dev_request("press - abcdefgh12345678 r4", kToken));
}

TEST(AfterglowDevRequests, RejectsWrongTokenAndMalformedLines)
{
    EXPECT_FALSE(parse_dev_request("quit - abcdefgh12345679 r1", kToken));
    EXPECT_FALSE(parse_dev_request("quit - abcdefgh12345678 r1", "")) << "off without a token";
    EXPECT_FALSE(parse_dev_request("quit - abcdefgh12345678 r1", "short"));
    EXPECT_FALSE(parse_dev_request("quit abcdefgh12345678 r1", kToken));
    EXPECT_FALSE(parse_dev_request("quit x abcdefgh12345678 r1", kToken));
    EXPECT_FALSE(parse_dev_request("tour all abcdefgh12345678 r1", kToken));
    EXPECT_FALSE(parse_dev_request("quit - abcdefgh12345678 r1 extra", kToken));
    EXPECT_FALSE(parse_dev_request("quit  - abcdefgh12345678 r1", kToken));
    EXPECT_FALSE(parse_dev_request("quit - abcdefgh12345678 r1\nstatus", kToken));
    EXPECT_FALSE(parse_dev_request("", kToken));
}

TEST(AfterglowDevRequests, ScreenshotNamesCannotLeaveTheDevFolder)
{
    EXPECT_FALSE(parse_dev_request("screenshot ../prefs abcdefgh12345678 r1", kToken));
    EXPECT_FALSE(parse_dev_request("screenshot a/b abcdefgh12345678 r1", kToken));
    EXPECT_FALSE(
        parse_dev_request("screenshot " + std::string(49, 'a') + " abcdefgh12345678 r1", kToken));
    EXPECT_FALSE(parse_dev_request("screenshot shot abcdefgh12345678 r.1", kToken));
}

TEST(AfterglowDevRequests, KeepsTheLastEightScreenshots)
{
    std::vector<std::string> kept;
    for (int i = 0; i < 8; ++i)
        EXPECT_TRUE(hui::afterglow::keep_dev_screenshot(kept, "s" + std::to_string(i)).empty());
    EXPECT_TRUE(hui::afterglow::keep_dev_screenshot(kept, "s0").empty())
        << "a reused name replaces its file and becomes the newest";
    EXPECT_EQ(hui::afterglow::keep_dev_screenshot(kept, "s8"), std::vector<std::string>{"s1"});
    ASSERT_EQ(kept.size(), hui::afterglow::kMaxDevScreenshots);
    EXPECT_EQ(kept.front(), "s2");
    EXPECT_EQ(kept.back(), "s8");
}

TEST(AfterglowDevRequests, BmpIsBottomUpBgrWithPaddedRows)
{
    // 2 x 2, bottom row first as glReadPixels returns it.
    const std::uint8_t rgba[] = {1, 2, 3, 255, 4, 5, 6, 255, 7, 8, 9, 255, 10, 11, 12, 255};
    const auto bmp = hui::afterglow::encode_bmp(2, 2, rgba);
    ASSERT_EQ(bmp.size(), 54u + 8u * 2u);
    EXPECT_EQ(bmp[0], 'B');
    EXPECT_EQ(bmp[1], 'M');
    EXPECT_EQ(bmp[2], bmp.size());
    EXPECT_EQ(bmp[10], 54);
    EXPECT_EQ(bmp[18], 2);
    EXPECT_EQ(bmp[22], 2);
    EXPECT_EQ(bmp[28], 24);
    EXPECT_EQ(bmp[34], 16);
    const std::vector<std::uint8_t> first_row(bmp.begin() + 54, bmp.begin() + 62);
    EXPECT_EQ(first_row, (std::vector<std::uint8_t>{3, 2, 1, 6, 5, 4, 0, 0}));
    EXPECT_EQ(bmp[62], 9);
    EXPECT_TRUE(hui::afterglow::encode_bmp(0, 2, rgba).empty());
    EXPECT_TRUE(hui::afterglow::encode_bmp(2, 2, nullptr).empty());
}

TEST(AfterglowDevRequests, StatusIsOneJsonObject)
{
    hui::afterglow::DevStatus status;
    status.id = "r1";
    status.version = "1.5.0";
    status.fps = 59.94f;
    status.connected = true;
    status.width = 1920;
    status.height = 1080;
    status.refresh_hz = 60;
    status.display_mode = 2;
    status.page = "queue \"x\"";
    const auto text = hui::afterglow::dev_status_json(status);
    const hui::afterglow::Json json(text);
    ASSERT_TRUE(json.valid()) << text;
    EXPECT_NE(text.find("\"version\":\"1.5.0\""), std::string::npos) << text;
    EXPECT_NE(text.find("\"fps\":59.9"), std::string::npos) << text;
    EXPECT_NE(text.find("\"connected\":true,\"playing\":false"), std::string::npos) << text;
    EXPECT_NE(text.find("\"width\":1920,\"height\":1080,\"refresh_hz\":60,\"mode\":2"),
              std::string::npos)
        << text;
    EXPECT_NE(text.find("\"page\":\"queue \\\"x\\\"\""), std::string::npos) << text;
}
