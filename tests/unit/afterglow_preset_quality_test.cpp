// Afterglow - Preset performance sampling and crash recovery regression tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/preset_quality.hpp"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <vector>

#ifndef HUI_SOURCE_DIR
#define HUI_SOURCE_DIR "."
#endif

using hui::afterglow::PresetQuality;

namespace
{
// The display names of the packaged library, which is what the scan records.
std::vector<std::string> library_names()
{
    std::vector<std::string> names;
    std::ifstream in(std::string(HUI_SOURCE_DIR) + "/assets/presets/index.tsv");
    std::string line;
    while (std::getline(in, line))
        if (const auto tab = line.find('\t'); !line.empty() && line[0] != '#' && tab != line.npos)
            names.push_back(line.substr(tab + 1));
    return names;
}
} // namespace

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

TEST(AfterglowPresetQuality, RoundTripsTheWholeLibraryWithLongNames)
{
    auto names = library_names();
    ASSERT_GT(names.size(), 390U);
    // The packaged names are short; padding them to 120 bytes leaves headroom.
    for (std::size_t i = names.size(); i < 395; ++i)
        names.push_back("Extra preset " + std::to_string(i));
    PresetQuality quality;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        names[i].resize(std::max<std::size_t>(names[i].size(), 120), '~');
        quality.record(names[i], 30 + static_cast<double>(i % 31));
        quality.set_keep(names[i], i % 7 == 0);
    }
    quality.begin(names.back());
    const auto encoded = quality.encode();
    ASSERT_GT(encoded.size(), 50000U);
    const auto restored = PresetQuality::decode(encoded);
    ASSERT_EQ(restored.ratings().size(), names.size());
    for (std::size_t i = 0; i + 1 < names.size(); ++i)
    {
        const auto &rating = restored.ratings().at(names[i]);
        EXPECT_NEAR(rating.fps, 30 + static_cast<double>(i % 31), 0.001) << names[i];
        EXPECT_EQ(rating.keep, i % 7 == 0) << names[i];
        EXPECT_FALSE(rating.quarantined) << names[i];
    }
    EXPECT_TRUE(restored.ratings().at(names.back()).quarantined);
    EXPECT_EQ(PresetQuality::decode(restored.encode()).encode(), restored.encode());
}

TEST(AfterglowPresetQuality, EncodesNoMoreThanTheNextStartReads)
{
    PresetQuality quality;
    // 1200 records of about a kilobyte: far past both limits.
    for (int i = 0; i < 1200; ++i)
    {
        auto name = "Preset " + std::to_string(1000 + i) + " \"quoted\" ";
        name.resize(1000, '~');
        quality.record(name, 60);
    }
    quality.begin("still pending");
    const auto encoded = quality.encode();
    EXPECT_LE(encoded.size(), 262144U);
    bool ok = false;
    const auto restored = PresetQuality::decode(encoded, &ok);
    EXPECT_TRUE(ok);
    EXPECT_GT(restored.ratings().size(), 200U);
    EXPECT_TRUE(restored.ratings().at("still pending").quarantined)
        << "the checkpoint is never the part left out";
    EXPECT_EQ(PresetQuality::decode(restored.encode()).encode(), restored.encode());

    PresetQuality many;
    for (int i = 0; i < 1200; ++i)
        many.record("P" + std::to_string(i), 60);
    EXPECT_EQ(PresetQuality::decode(many.encode()).ratings().size(), PresetQuality::kMaxRatings);
}

TEST(AfterglowPresetQuality, SaysWhenADocumentCouldNotBeRead)
{
    bool ok = false;
    PresetQuality::decode("", &ok);
    EXPECT_TRUE(ok) << "nothing saved yet";
    PresetQuality::decode(PresetQuality{}.encode(), &ok);
    EXPECT_TRUE(ok);
    PresetQuality::decode("{\"version\":1,\"ratings\":\"rating=nan\"}", &ok);
    EXPECT_TRUE(ok) << "a bad record is skipped, the document is read";
    for (const std::string &text :
         {std::string("not json"), std::string("{\"version\":2}"), std::string(300000, ' ')})
    {
        ok = true;
        EXPECT_TRUE(PresetQuality::decode(text, &ok).ratings().empty());
        EXPECT_FALSE(ok) << text.substr(0, 20);
    }
}

TEST(AfterglowPresetQuality, KeepsAQuarantineThatSortsLast)
{
    PresetQuality quality;
    for (int i = 0; i < 395; ++i)
        quality.record("Preset " + std::to_string(1000 + i), 60);
    quality.quarantine("zzz crashed the console");
    const auto restored = PresetQuality::decode(quality.encode());
    ASSERT_EQ(restored.ratings().size(), 396U);
    EXPECT_TRUE(restored.ratings().at("zzz crashed the console").quarantined);
    EXPECT_FALSE(restored.eligible("zzz crashed the console", 30, false));
}
