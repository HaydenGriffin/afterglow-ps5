// Afterglow - Light bar colour mapping and write rate limit.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/lightbar.hpp"
#include <gtest/gtest.h>
#include <limits>

using hui::afterglow::lightbar_color;
using hui::afterglow::LightbarColor;
using hui::afterglow::LightbarInput;
using hui::afterglow::LightbarLimiter;

TEST(AfterglowLightbar, DarkCoverIsLiftedToFullBrightnessKeepingItsHue)
{
    LightbarInput input;
    input.artwork = {0.20f, 0.10f, 0.05f};
    input.playing = true;
    EXPECT_EQ(lightbar_color(input), (LightbarColor{255, 128, 64}));
}

TEST(AfterglowLightbar, BlackFallsBackToAmber)
{
    LightbarInput input;
    input.artwork = {0.0f, 0.0f, 0.005f};
    input.playing = true;
    EXPECT_EQ(lightbar_color(input), (LightbarColor{238, 184, 117}));
}

TEST(AfterglowLightbar, IdleIsDim)
{
    LightbarInput input;
    input.artwork = {0.0f, 0.5f, 1.0f};
    const auto idle = lightbar_color(input);
    EXPECT_EQ(idle.r, 0);
    EXPECT_GT(idle.b, 0);
    EXPECT_LT(idle.b, 40);
}

TEST(AfterglowLightbar, BeatPulsesBetweenAFloorAndFull)
{
    LightbarInput input;
    input.artwork = {1.0f, 1.0f, 1.0f};
    input.playing = true;
    input.beat_enabled = true;
    input.beat = 0;
    const auto rest = lightbar_color(input);
    input.beat = 1;
    const auto peak = lightbar_color(input);
    input.beat = 7; // clamped
    EXPECT_EQ(lightbar_color(input), peak);
    EXPECT_EQ(peak, (LightbarColor{255, 255, 255}));
    EXPECT_GT(rest.r, 40);
    EXPECT_LT(rest.r, 128);
}

TEST(AfterglowLightbar, NonFiniteArtworkIsAmber)
{
    LightbarInput input;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    input.artwork = {nan, nan, nan};
    input.playing = true;
    EXPECT_EQ(lightbar_color(input), (LightbarColor{238, 184, 117}));
}

TEST(AfterglowLightbar, LimiterWritesOnlyChangesAtMostThirtyTimesASecond)
{
    LightbarLimiter limiter;
    const LightbarColor red{255, 0, 0}, blue{0, 0, 255};
    EXPECT_TRUE(limiter.due(red, 0));
    limiter.wrote(red, 0);
    EXPECT_FALSE(limiter.due(red, 1000000)) << "unchanged colour is never rewritten";
    EXPECT_FALSE(limiter.due(blue, 10000)) << "too soon";
    EXPECT_TRUE(limiter.due(blue, LightbarLimiter::kMinIntervalUs));
    limiter.wrote(blue, LightbarLimiter::kMinIntervalUs);
    limiter.forget();
    EXPECT_TRUE(limiter.due(blue, LightbarLimiter::kMinIntervalUs + 1)) << "after a reset";
}
