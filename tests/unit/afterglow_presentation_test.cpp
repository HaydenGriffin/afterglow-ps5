// Afterglow - Output spectrum and controller behavior regression tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/presentation.hpp"
#include "concept_fixture.hpp"
#include "concepts/concepts.hpp"
#include <algorithm>
#include <cmath>

TEST(AfterglowAnalyzer, SilenceIsSilent)
{
    hui::afterglow::Analyzer analyzer;
    std::array<std::int16_t, 4096> samples{};
    analyzer.push(samples.data(), 2048);
    for (float level : analyzer.update(0.04f))
        EXPECT_FLOAT_EQ(level, 0);
}

TEST(AfterglowAnalyzer, FindsOneKilohertzFromActualPcm)
{
    hui::afterglow::Analyzer analyzer;
    std::array<std::int16_t, 4096> samples{};
    for (int i = 0; i < 2048; ++i)
    {
        samples[static_cast<std::size_t>(i) * 2] =
            static_cast<std::int16_t>(15000 * std::sin(2 * 3.141592653589793 * 1000 * i / 48000));
        samples[static_cast<std::size_t>(i) * 2 + 1] = samples[static_cast<std::size_t>(i) * 2];
    }
    analyzer.push(samples.data(), 2048);
    const auto levels = analyzer.update(0.04f);
    const auto peak = std::distance(levels.begin(), std::max_element(levels.begin(), levels.end()));
    EXPECT_GE(peak, 16);
    EXPECT_LE(peak, 18);
    EXPECT_GT(levels[static_cast<std::size_t>(peak)], 0.8f);
    for (float value : levels)
    {
        EXPECT_GE(value, 0);
        EXPECT_LE(value, 1);
    }
    const auto decayed = analyzer.update(1.0f);
    EXPECT_LT(decayed[static_cast<std::size_t>(peak)], 0.01f);
}

namespace
{
// Feeds the analyser as the screen does: 1/30 s of audio, then one update.
// A 55 Hz kick every period seconds over a quiet bass hum.
int count_beats(hui::afterglow::Analyzer &analyzer, double period, double seconds,
                float *peak_after = nullptr, double amplitude = 0.8)
{
    constexpr int kFrames = 1600;
    std::array<std::int16_t, kFrames * 2> samples{};
    int beats = 0;
    float previous = 0;
    long sample = 0;
    for (int step = 0; step < static_cast<int>(seconds * 30); ++step)
    {
        for (int i = 0; i < kFrames; ++i, ++sample)
        {
            const double t = static_cast<double>(sample) / 48000;
            const double since = std::fmod(t, period);
            const double kick =
                period > 0 ? std::sin(2 * 3.141592653589793 * 55 * since) * std::exp(-since * 14)
                           : 0;
            const double hum = 0.04 * std::sin(2 * 3.141592653589793 * 2000 * t);
            const auto value = static_cast<std::int16_t>((amplitude * kick + hum) * 30000);
            samples[static_cast<std::size_t>(i) * 2] = value;
            samples[static_cast<std::size_t>(i) * 2 + 1] = value;
        }
        analyzer.push(samples.data(), kFrames);
        analyzer.update(1.0f / 30);
        const float beat = analyzer.beat();
        if (beat > 0.99f && previous < 0.99f)
            ++beats;
        previous = beat;
    }
    if (peak_after != nullptr)
        *peak_after = analyzer.beat();
    return beats;
}
} // namespace

TEST(AfterglowAnalyzer, BeatPulsesOnEachKick)
{
    hui::afterglow::Analyzer analyzer;
    const int beats = count_beats(analyzer, 0.5, 4.0);
    EXPECT_GE(beats, 6) << "120 bpm for four seconds";
    EXPECT_LE(beats, 9);
}

TEST(AfterglowAnalyzer, BeatDecaysInAboutAQuarterSecond)
{
    hui::afterglow::Analyzer analyzer;
    count_beats(analyzer, 0.5, 1.0);
    analyzer.set_beat_sensitivity(100);
    std::array<std::int16_t, 3200> silence{};
    // Find a fresh pulse, then let 250 ms of silence pass.
    float level = 0;
    for (int i = 0; i < 60 && level < 0.99f; ++i)
    {
        count_beats(analyzer, 0.5, 1.0 / 30, &level);
    }
    ASSERT_GT(level, 0.99f);
    for (int i = 0; i < 8; ++i)
    {
        analyzer.push(silence.data(), 1600);
        analyzer.update(1.0f / 32);
    }
    EXPECT_LT(analyzer.beat(), 0.1f);
    EXPECT_GT(analyzer.beat(), 0.0f);
}

TEST(AfterglowAnalyzer, SteadyToneAndZeroSensitivityNeverBeat)
{
    hui::afterglow::Analyzer steady;
    count_beats(steady, 0, 1.0);
    EXPECT_EQ(count_beats(steady, 0, 3.0), 0) << "a hum is not a beat";
    hui::afterglow::Analyzer off;
    off.set_beat_sensitivity(0);
    EXPECT_EQ(count_beats(off, 0.5, 3.0), 0);
}

TEST(AfterglowAnalyzer, HigherSensitivityFindsSofterKicks)
{
    // A soft kick that a quarter of the default sensitivity misses.
    constexpr double kSoftKick = 0.05;
    hui::afterglow::Analyzer low, high;
    low.set_beat_sensitivity(25);
    high.set_beat_sensitivity(200);
    const int soft = count_beats(low, 0.5, 4.0, nullptr, kSoftKick);
    const int keen = count_beats(high, 0.5, 4.0, nullptr, kSoftKick);
    EXPECT_LT(soft, keen);
}

class AfterglowScreen : public hui::testing::ConceptFixture
{
};

TEST_F(AfterglowScreen, ControlsEmitCommandsAndNeverInventPlayback)
{
    auto &state = hui::afterglow::presentation();
    state = {};
    auto screen = hui::concepts::make_afterglow(context_);
    hui::InputFrame input;
    input.pressed = hui::action_bit(hui::Action::confirm);
    hui::app::Feedback feedback;
    screen->update(input, 0.02f, feedback);
    EXPECT_EQ(state.command, hui::afterglow::UiCommand::toggle);
    EXPECT_FALSE(state.playing);
    input.pressed = hui::action_bit(hui::Action::west);
    screen->update(input, 0.02f, feedback);
    expect_drawable(*screen);
    input.pressed = 0;
    input.held = hui::action_bit(hui::Action::back);
    for (int i = 0; i < 90; ++i)
        screen->update(input, 0.02f, feedback);
    EXPECT_TRUE(state.exit_requested);
    state = {};
}

TEST(AfterglowAnalyzer, PcmIsDeliveredOnceInStereoOrder)
{
    hui::afterglow::Analyzer analyzer;
    const std::array<std::int16_t, 4> samples{16384, -16384, 8192, -8192};
    analyzer.push(samples.data(), 2);
    analyzer.update(0.04f);
    ASSERT_EQ(analyzer.fresh_pcm().size(), 4u);
    EXPECT_FLOAT_EQ(analyzer.fresh_pcm()[0], 0.5f);
    EXPECT_FLOAT_EQ(analyzer.fresh_pcm()[1], -0.5f);
    EXPECT_FLOAT_EQ(analyzer.fresh_pcm()[2], 0.25f);
    EXPECT_FLOAT_EQ(analyzer.fresh_pcm()[3], -0.25f);
    analyzer.update(0.01f);
    EXPECT_TRUE(analyzer.fresh_pcm().empty());
}

TEST_F(AfterglowScreen, PresetControlsOnlyOperateInMilkdropMode)
{
    auto &state = hui::afterglow::presentation();
    state = {};
    auto screen = hui::concepts::make_afterglow(context_);
    hui::InputFrame input;
    hui::app::Feedback feedback;
    input.pressed = hui::action_bit(hui::Action::jump_next);
    screen->update(input, 0.02f, feedback);
    EXPECT_EQ(state.command, hui::afterglow::UiCommand::none);
    state.milkdrop_texture = 1;
    screen->update(input, 0.02f, feedback);
    EXPECT_EQ(state.command, hui::afterglow::UiCommand::preset_next);
    input.pressed = hui::action_bit(hui::Action::jump_prev);
    screen->update(input, 0.02f, feedback);
    EXPECT_EQ(state.command, hui::afterglow::UiCommand::preset_previous);
    // The visual style is a Display setting now; Triangle belongs to the lyrics.
    for (int style = 0; style < hui::afterglow::kVisualStyleCount; ++style)
    {
        state.command = hui::afterglow::UiCommand::none;
        state.preferences.visual_style = style;
        input.pressed = 0;
        screen->update(input, 0.02f, feedback);
        EXPECT_EQ(state.milkdrop_visible, style == 0);
        input.pressed = hui::action_bit(hui::Action::jump_next);
        screen->update(input, 0.02f, feedback);
        EXPECT_EQ(state.command, style == 0 ? hui::afterglow::UiCommand::preset_next
                                            : hui::afterglow::UiCommand::none);
    }
    state = {};
}
