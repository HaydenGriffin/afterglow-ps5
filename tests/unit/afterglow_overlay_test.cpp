// Afterglow - Lyric overlay layout, smooth progress and the sheets' scroll cue.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/lyric_overlay.hpp"
#include "afterglow/progress_clock.hpp"
#include "afterglow/sheet_list.hpp"
#include "concept_fixture.hpp"

namespace
{
using hui::Direction;
using hui::afterglow::overlay_scale;
using hui::afterglow::overlay_top;
using hui::afterglow::OverlayRoom;
using hui::afterglow::ProgressClock;
using hui::afterglow::SheetList;
using hui::afterglow::SheetRow;
namespace a = hui::afterglow;

// ---- Lyrics over visuals: size and position ----

OverlayRoom room()
{
    OverlayRoom value;
    value.top = 54;
    value.bottom = 980;
    return value;
}

TEST(AfterglowOverlayLayout, SizesScaleTodaysOverlayAndUnknownIsMedium)
{
    EXPECT_FLOAT_EQ(overlay_scale(0), 0.8f);
    EXPECT_FLOAT_EQ(overlay_scale(1), 1.0f);
    EXPECT_FLOAT_EQ(overlay_scale(2), 1.25f);
    EXPECT_FLOAT_EQ(overlay_scale(7), 1.0f);
}

TEST(AfterglowOverlayLayout, TopMiddleAndBottomStayInsideTheRoom)
{
    EXPECT_FLOAT_EQ(overlay_top(a::position_top, 184, room()), 54);
    EXPECT_FLOAT_EQ(overlay_top(a::position_middle, 184, room()), 540 - 92);
    EXPECT_FLOAT_EQ(overlay_top(a::position_bottom, 184, room()), 980 - 184);
    // Taller than the room: it starts at the top rather than off screen.
    EXPECT_FLOAT_EQ(overlay_top(a::position_bottom, 2000, room()), 54);
    EXPECT_FLOAT_EQ(overlay_top(a::position_middle, 2000, room()), 54);
}

TEST(AfterglowOverlayLayout, BottomStacksAboveTheTrackCardAsItFadesIn)
{
    OverlayRoom r = room();
    r.card_top = 760;
    r.card = 1;
    const float stacked = 760 - a::kOverlayGutter - 184;
    EXPECT_FLOAT_EQ(overlay_top(a::position_bottom, 184, r), stacked);
    // Half faded in: half way there, so it slides instead of jumping.
    r.card = 0.5f;
    EXPECT_FLOAT_EQ(overlay_top(a::position_bottom, 184, r), (stacked + 980 - 184) / 2);
    r.card = 0;
    EXPECT_FLOAT_EQ(overlay_top(a::position_bottom, 184, r), 980 - 184);
}

TEST(AfterglowOverlayLayout, MiddleAndTopMoveOnlyWhenTheCardWouldRunIntoThem)
{
    OverlayRoom r = room();
    r.card_top = 704;
    r.card = 1;
    // Stage at Middle, Medium: 387..693 clears a card from 704 by the gutter? No:
    // 693 > 704 - 16, so it moves up just enough.
    const float medium = 306;
    EXPECT_FLOAT_EQ(overlay_top(a::position_middle, medium, r), 704 - 16 - medium);
    // A small one-line field in the middle is clear of the card and stays centred.
    EXPECT_FLOAT_EQ(overlay_top(a::position_middle, 150, r), 540 - 75);
    // Large at the top never moves down or above the safe top.
    EXPECT_FLOAT_EQ(overlay_top(a::position_top, medium * 1.25f, r), 54);
    // Whatever the card, the overlay's bottom edge ends above it.
    for (int position = 0; position < 3; ++position)
        for (float height : {150.0f, 230.0f, 306.0f, 382.5f})
            EXPECT_LE(overlay_top(position, height, r) + height, 704 - a::kOverlayGutter + 0.01f)
                << position << " " << height;
}

// ---- Smooth progress ----

constexpr float kFrame = 1.0f / 60.0f;

TEST(AfterglowProgressClock, FirstReportAndNewSongsSnap)
{
    ProgressClock clock;
    EXPECT_DOUBLE_EQ(clock.update(42, true, "a", 240, kFrame, false), 42);
    EXPECT_DOUBLE_EQ(clock.update(3, true, "b", 240, kFrame, false), 3);
}

TEST(AfterglowProgressClock, MovesEveryFrameBetweenStepwiseReports)
{
    ProgressClock clock;
    clock.update(10, true, "a", 240, kFrame, false);
    // Reports change only every sixth frame (10 Hz); the clock moves on every one.
    double previous = clock.position();
    double reported = 10;
    for (int frame = 1; frame <= 120; ++frame)
    {
        if (frame % 6 == 0)
            reported += 0.1;
        const double now = clock.update(reported, true, "a", 240, kFrame, false);
        EXPECT_GT(now, previous) << frame;
        EXPECT_LT(now - previous, 0.05) << "no visible jump at frame " << frame;
        previous = now;
    }
    EXPECT_NEAR(previous, 12, 0.15);
}

TEST(AfterglowProgressClock, SmallCorrectionsEaseAndNeverGoBackwards)
{
    ProgressClock clock;
    clock.update(20, true, "a", 240, kFrame, false);
    for (int i = 0; i < 30; ++i)
        clock.update(20 + (i + 1) * kFrame, true, "a", 240, kFrame, false);
    // The server says it is 0.4 s earlier than we thought.
    const double before = clock.position();
    double reported = before - 0.4, previous = before;
    for (int i = 0; i < 60; ++i)
    {
        reported += kFrame;
        const double now = clock.update(reported, true, "a", 240, kFrame, false);
        EXPECT_GE(now, previous) << i;
        previous = now;
    }
    EXPECT_NEAR(previous, reported, 0.05) << "caught up within a second";
    // 0.6 s later than we thought: eased forward, not jumped.
    reported += 0.6;
    const double next = clock.update(reported, true, "a", 240, kFrame, false);
    EXPECT_LT(next - previous, 0.2);
    EXPECT_GT(next - previous, kFrame);
}

TEST(AfterglowProgressClock, SeeksSnapAndPausedHoldsStill)
{
    ProgressClock clock;
    clock.update(30, true, "a", 240, kFrame, false);
    EXPECT_DOUBLE_EQ(clock.update(40, true, "a", 240, kFrame, false), 40) << "a seek";
    EXPECT_DOUBLE_EQ(clock.update(29.5, true, "a", 240, kFrame, false), 29.5) << "a seek back";
    const double paused = clock.update(29.6, false, "a", 240, kFrame, false);
    for (int i = 0; i < 60; ++i)
        EXPECT_DOUBLE_EQ(clock.update(29.6, false, "a", 240, kFrame, false), paused);
    EXPECT_DOUBLE_EQ(clock.update(90, false, "a", 240, kFrame, false), 90) << "seek while paused";
}

TEST(AfterglowProgressClock, AShortSeekWhilePausedShowsAtOnce)
{
    ProgressClock clock;
    clock.update(60, false, "a", 240, kFrame, false);
    EXPECT_DOUBLE_EQ(clock.update(61, false, "a", 240, kFrame, false), 61) << "ahead one second";
    EXPECT_DOUBLE_EQ(clock.update(60.5, false, "a", 240, kFrame, false), 60.5) << "back half";
    for (int i = 0; i < 30; ++i)
        EXPECT_DOUBLE_EQ(clock.update(60.5, false, "a", 240, kFrame, false), 60.5);
}

TEST(AfterglowProgressClock, ReducedMotionFollowsWithoutEasingAndStopsAtTheEnd)
{
    ProgressClock clock;
    clock.update(50, true, "a", 240, kFrame, true);
    const double next = clock.update(50.5, true, "a", 240, kFrame, true);
    EXPECT_DOUBLE_EQ(next, 50.5) << "small forward correction taken at once";
    EXPECT_DOUBLE_EQ(clock.update(50.2, true, "a", 240, kFrame, true), 50.5)
        << "ahead: waits instead of stepping back";
    ProgressClock end;
    end.update(239.9, true, "a", 240, kFrame, false);
    for (int i = 0; i < 30; ++i)
        end.update(239.9 + (i + 1) * kFrame, true, "a", 240, kFrame, false);
    EXPECT_LE(end.position(), 240);
}

TEST(AfterglowProgressClock, RunsOnAtMostASecondPastSilentReports)
{
    ProgressClock clock;
    clock.update(10, true, "a", 240, kFrame, false);
    for (int i = 0; i < 600; ++i)
        clock.update(10, true, "a", 240, kFrame, false);
    EXPECT_NEAR(clock.position(), 11, 0.02);
}

// ---- The sheets' scroll cue ----

class AfterglowSheetCue : public hui::testing::ConceptFixture
{
  protected:
    void SetUp() override
    {
        std::vector<SheetRow> rows;
        for (int i = 0; i < 20; ++i)
        {
            SheetRow row;
            // One word for every row: a frame's glyphs then count "More" alone.
            row.item.title = "Row";
            row.item.tag = i;
            rows.push_back(std::move(row));
        }
        list_.set_bounds({680, 200, 1144, 76 * 6});
        list_.set_rows(std::move(rows));
    }
    void step(Direction direction)
    {
        hui::ui::Feedback feedback;
        list_.handle(nav(direction), feedback);
        list_.update(0.02f);
    }
    int glyphs() const
    {
        hui::gfx::DrawList list;
        hui::ui::Canvas canvas{list, fonts_, 0, 0};
        list_.draw(canvas);
        int count = 0;
        for (const auto &shape : list.instances())
            count += shape.params[3] == 1.0f;
        return count;
    }
    SheetList list_;
};

TEST_F(AfterglowSheetCue, FocusKeepsARowOfContextBelowAndAbove)
{
    EXPECT_EQ(list_.first_shown(), 0);
    EXPECT_EQ(list_.shown_count(), 6);
    for (int i = 0; i < 18; ++i)
    {
        step(Direction::down);
        const int last = list_.first_shown() + list_.shown_count() - 1;
        if (last < 19)
            EXPECT_LT(list_.focus(), last) << "the faded bottom row is never the focused one";
        if (list_.first_shown() > 0)
            EXPECT_GT(list_.focus(), list_.first_shown()) << "nor the faded top row";
    }
    step(Direction::down);
    EXPECT_EQ(list_.focus(), 19);
    EXPECT_EQ(list_.first_shown() + list_.shown_count(), 20) << "the last row shows whole";
    for (int i = 0; i < 18; ++i)
    {
        step(Direction::up);
        if (list_.first_shown() > 0)
            EXPECT_GT(list_.focus(), list_.first_shown());
    }
}

TEST_F(AfterglowSheetCue, MoreShowsWhileRowsAreHiddenAndGoesWhenAllShow)
{
    const int at_top = glyphs();
    for (int i = 0; i < 19; ++i)
        step(Direction::down);
    const int at_bottom = glyphs();
    for (int i = 0; i < 9; ++i)
        step(Direction::up);
    const int middle = glyphs();
    // Six rows of "Row" each time; "More" adds four glyphs once at either end
    // and twice in between.
    EXPECT_EQ(at_top, 6 * 3 + 4);
    EXPECT_EQ(middle - at_top, 4);
    EXPECT_EQ(middle - at_bottom, 4);
    std::vector<SheetRow> few;
    for (int i = 0; i < 3; ++i)
    {
        SheetRow row;
        row.item.title = "Row";
        few.push_back(std::move(row));
    }
    list_.set_rows(std::move(few));
    EXPECT_EQ(glyphs(), 3 * 3) << "three rows and no cue";
}
} // namespace
