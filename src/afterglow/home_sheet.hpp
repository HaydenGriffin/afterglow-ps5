// Afterglow - The Home sheet: a Home Assistant glance with pressable tiles.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/home_model.hpp"
#include "afterglow/sheet_list.hpp"
#include <map>

namespace hui::afterglow
{
// Panel delegates Page::home to this. It reads presentation().home and pushes
// presses onto presentation().home_commands.
//
// Rows are the configured groups: a heading per group, then one row per
// entity with its value on the right. Lights, switches, fans and
// input_booleans carry a drawn tick while on and toggle with Cross; scenes and
// scripts run with Cross. A press shows its new state at once and reverts if
// Home Assistant refuses it or the state does not follow within a few seconds.
class HomeSheet
{
  public:
    HomeSheet();
    // Called when Panel opens the page.
    void open();
    // D-pad and Cross; Panel keeps L1/R1, Options and Circle.
    void update(const InputFrame &input, float dt, ui::Feedback &feedback,
                bool reduced_motion = false);
    // Where the rows go; Panel sets it before update so draw() stays pure.
    void set_area(const gfx::Rect &area);
    // The area right of the artwork slab under Panel's title and rule: the
    // rows, or a headline and the words for what to do when there are none.
    // A refused press is named on the line below the area.
    void draw(ui::Canvas &canvas, float x, float y, float width, float height) const;
    // Up one level; false at the top.
    bool back();
    bool covers_screen() const;
    // The page note under the title, e.g. "Connected · 24 entities".
    std::string note() const;
    // The rows as shown (headings included); for tests and the screen.
    const std::vector<SheetRow> &rows() const
    {
        return list_.rows();
    }
    // The press refusal on the notice line; empty when none.
    const std::string &notice() const
    {
        return notice_;
    }

    // How long a press waits for Home Assistant to report the new state.
    static constexpr float kOptimisticSeconds = 5;

  private:
    struct Override
    {
        bool on = false;
        float age = 0;
    };
    void rebuild();
    void read_results();
    void press(int row, ui::Feedback &feedback);
    const HomeEntity *entity(const std::string &entity_id) const;
    bool showing_rows() const;

    SheetList list_;
    // The entity id behind each row; empty for headings.
    std::vector<std::string> row_entities_;
    // The area draw() was last given; the list takes it on the next update.
    gfx::Rect area_;
    std::map<std::string, Override> overrides_;
    std::uint64_t seen_result_ = 0, built_revision_ = ~0ULL;
    std::string notice_;
    float notice_age_ = 0;
    bool dirty_ = true;
};
} // namespace hui::afterglow
