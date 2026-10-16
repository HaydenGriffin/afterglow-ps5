// Afterglow - Album colour rewriting of MilkDrop preset colour slots.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/night.hpp"
#include "afterglow/preset_colour.hpp"
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

namespace
{
using hui::afterglow::recolour_preset;
using hui::afterglow::Rgb;

// The value of key in the preset text, or -1.
float value(const std::string &preset, const std::string &key)
{
    const auto at = preset.find("\n" + key + "=");
    if (at == std::string::npos)
        return -1;
    return std::strtof(preset.c_str() + at + key.size() + 2, nullptr);
}
float luma(float r, float g, float b)
{
    return r * 0.3f + g * 0.59f + b * 0.11f;
}
const Rgb kBlue{0.16f, 0.25f, 0.72f};
const Rgb kGrey{0.243f, 0.259f, 0.282f};
const std::string kPreset = "[preset00]\nfDecay=0.98\nwave_r=0.8\nwave_g=0.8\nwave_b=0.8\n"
                            "ob_size=0.01\nob_r=0.2\nob_g=0.2\nob_b=0.2\nib_size=0.3\n"
                            "ib_r=0.9\nib_g=0.1\nib_b=0.1\nper_frame_1=wave_r = 0.5;\n";
} // namespace

TEST(AfterglowPresetColour, TakesTheArtworkHueAtThePresetsBrightness)
{
    const auto out = recolour_preset(kPreset, kBlue, kBlue, 1);
    const float r = value(out, "wave_r"), g = value(out, "wave_g"), b = value(out, "wave_b");
    EXPECT_NEAR(luma(r, g, b), luma(0.8f, 0.8f, 0.8f), 0.01f) << "brightness kept";
    EXPECT_GT(b, r) << "blue hue taken";
    EXPECT_GT(b, g);
    EXPECT_NE(out.find("per_frame_1=wave_r = 0.5;"), std::string::npos)
        << "equations are left alone (and can still override)";
    EXPECT_NE(out.find("fDecay=0.98"), std::string::npos);
}

TEST(AfterglowPresetColour, WideBordersKeepTheirColourAndMissingSlotsAreAdded)
{
    const auto out = recolour_preset(kPreset, kBlue, kBlue, 1);
    EXPECT_FLOAT_EQ(value(out, "ib_r"), 0.9f) << "a 0.3 inner border is a wash, not a frame";
    EXPECT_NE(value(out, "ob_b"), 0.2f) << "a thin outer border is recoloured";
    // mv_* were absent: MilkDrop's white default, recoloured at white's brightness.
    ASSERT_GE(value(out, "mv_b"), 0);
    EXPECT_NEAR(luma(value(out, "mv_r"), value(out, "mv_g"), value(out, "mv_b")), 1.0f, 0.02f);
}

TEST(AfterglowPresetColour, StrengthBlendsFromThePresetsOwnColours)
{
    EXPECT_EQ(recolour_preset(kPreset, kBlue, kBlue, 0), kPreset);
    const float full = value(recolour_preset(kPreset, kBlue, kBlue, 1), "wave_b");
    const float half = value(recolour_preset(kPreset, kBlue, kBlue, 0.5f), "wave_b");
    EXPECT_NEAR(half, (0.8f + full) / 2, 0.001f);
}

TEST(AfterglowPresetColour, GreyArtworkLeavesThePresetAlone)
{
    EXPECT_FALSE(hui::afterglow::has_hue(kGrey));
    EXPECT_TRUE(hui::afterglow::has_hue(kBlue));
    EXPECT_EQ(recolour_preset(kPreset, kGrey, kGrey, 1), kPreset);
    // One colourful colour of the two is enough; it fills every slot.
    const auto out = recolour_preset(kPreset, kGrey, kBlue, 1);
    EXPECT_GT(value(out, "wave_b"), value(out, "wave_r"));
}

TEST(AfterglowPresetColour, KeepsCrlfLineEndings)
{
    std::string crlf;
    for (char c : kPreset)
        crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    const auto out = recolour_preset(crlf, kBlue, kBlue, 1);
    for (std::size_t i = 0; i < out.size(); ++i)
        if (out[i] == '\n')
            ASSERT_TRUE(i > 0 && out[i - 1] == '\r') << "bare LF at " << i;
    EXPECT_NE(out.find("\r\nmv_r="), std::string::npos);
    EXPECT_GT(value(out, "wave_b"), value(out, "wave_r"));
}

TEST(AfterglowNight, RegisterHoursAreTenAtNightToSixInTheMorning)
{
    using hui::afterglow::night_hour;
    EXPECT_FALSE(night_hour(21));
    EXPECT_TRUE(night_hour(22));
    EXPECT_TRUE(night_hour(0));
    EXPECT_TRUE(night_hour(5));
    EXPECT_FALSE(night_hour(6));
    EXPECT_FALSE(night_hour(-1)) << "an unreadable clock is day";
    const int hour = hui::afterglow::local_hour();
    EXPECT_GE(hour, 0);
    EXPECT_LT(hour, 24);
}
