// Afterglow - Preset performance sampling and crash recovery regression tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/preset_quality.hpp"
#include <gtest/gtest.h>
#include <limits>

using hui::afterglow::PresetQuality;

TEST(AfterglowPresetQuality, ExcludesWarmupAndMeasuresPresentedFramesOnce)
{
    PresetQuality quality;
    quality.begin("slow shader then smooth");
    EXPECT_FALSE(quality.observe(0.6));
    for (int i = 0; i < 299; ++i)
        EXPECT_FALSE(quality.observe(1.0 / 60));
    EXPECT_TRUE(quality.observe(1.0 / 60));
    ASSERT_EQ(quality.ratings().size(), 1U);
    EXPECT_NEAR(quality.ratings().at("slow shader then smooth").fps, 60, 0.001);
    EXPECT_TRUE(quality.pending().empty());
    EXPECT_FALSE(quality.observe(1));
}

TEST(AfterglowPresetQuality, SlowFramesCountButInvalidTimesDoNot)
{
    PresetQuality quality;
    quality.begin("mixed");
    EXPECT_FALSE(quality.observe(0.5));
    EXPECT_FALSE(quality.observe(-2));
    EXPECT_FALSE(quality.observe(std::numeric_limits<double>::quiet_NaN()));
    for (int i = 0; i < 99; ++i)
        EXPECT_FALSE(quality.observe(0.05));
    EXPECT_TRUE(quality.observe(0.05));
    EXPECT_NEAR(quality.ratings().at("mixed").fps, 20, 0.001);
}

TEST(AfterglowPresetQuality, ExplicitCancellationDoesNotQuarantineOrRetainPartialSample)
{
    PresetQuality quality;
    quality.begin("cancelled");
    EXPECT_FALSE(quality.observe(0.5));
    EXPECT_FALSE(quality.observe(4));
    quality.cancel();
    EXPECT_TRUE(quality.pending().empty());
    EXPECT_FALSE(quality.observe(10));
    const auto restored = PresetQuality::decode(quality.encode());
    EXPECT_TRUE(restored.ratings().empty());
    EXPECT_TRUE(restored.eligible("cancelled", 60, true));
    quality.begin("new sample");
    EXPECT_FALSE(quality.observe(0.5));
    EXPECT_FALSE(quality.observe(1));
    EXPECT_TRUE(quality.observe(4));
    EXPECT_DOUBLE_EQ(quality.ratings().at("new sample").fps, 0.4);
}

TEST(AfterglowPresetQuality, FiltersMeasuredPresetsWithToleranceAndManualOverrides)
{
    PresetQuality quality;
    EXPECT_TRUE(quality.eligible("unknown", 60, true));
    quality.record("fast", 59.6);
    quality.record("slow", 39.4);
    EXPECT_TRUE(quality.eligible("fast", 60, true));
    EXPECT_FALSE(quality.eligible("slow", 40, true));
    EXPECT_TRUE(quality.eligible("slow", 30, true));
    EXPECT_TRUE(quality.eligible("slow", 60, false));
    quality.quarantine("slow");
    EXPECT_FALSE(quality.eligible("slow", 30, false));
    quality.set_keep("slow", true);
    EXPECT_TRUE(quality.eligible("slow", 60, true));
    quality.set_keep("slow", false);
    EXPECT_FALSE(quality.eligible("slow", 60, false));
    quality.record("slow", 45);
    EXPECT_TRUE(quality.eligible("slow", 45, true));
}

TEST(AfterglowPresetQuality, PersistsRatingsAndQuarantinesInterruptedScan)
{
    PresetQuality quality;
    quality.record("completed", 59.93);
    quality.set_keep("manual", true);
    quality.begin("interrupted");
    EXPECT_EQ(quality.encode().find('\n'), std::string::npos);
    const auto restored = PresetQuality::decode(quality.encode());
    EXPECT_NEAR(restored.ratings().at("completed").fps, 59.93, 0.001);
    EXPECT_TRUE(restored.ratings().at("manual").keep);
    EXPECT_TRUE(restored.pending().empty());
    EXPECT_TRUE(restored.ratings().at("interrupted").quarantined);
    EXPECT_FALSE(restored.eligible("interrupted", 30, false));
    EXPECT_EQ(PresetQuality::decode(restored.encode()).encode(), restored.encode());
}

TEST(AfterglowPresetQuality, RejectsMalformedRecordsAndUnsupportedVersion)
{
    EXPECT_TRUE(
        PresetQuality::decode(R"({"version":2,"ratings":"rating=60000\t1\t0\t0\tPreset\n"})")
            .ratings()
            .empty());
    const auto quality = PresetQuality::decode(
        R"({"version":1,"ratings":"rating=nan\t1\t0\t0\tBad\nrating=60000\t2\t0\t0\tBad flag\nrating=60000\t1\t0\t0\tValid\r\n"})");
    ASSERT_EQ(quality.ratings().size(), 1U);
    EXPECT_DOUBLE_EQ(quality.ratings().at("Valid").fps, 60);
}
