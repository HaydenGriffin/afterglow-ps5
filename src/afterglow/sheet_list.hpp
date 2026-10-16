// Afterglow - Hairline-ruled sheet rows that page by whole rows, never slivers.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "afterglow/style.hpp"
#include "ui/components/list.hpp"
#include <string>
#include <vector>

namespace hui::afterglow
{
// One row of a sheet. The ListItem carries the words, the action tag and the
// header/disabled flags; the rest is what the LG rows show beside them.
struct SheetRow
{
    ui::ListItem item;
    bool chosen = false;     // the current choice: a drawn tick before the label
    bool adjustable = false; // left/right changes the value: chevrons on the focused row
    bool thumbnail = false;  // an artwork square before the words
    std::string artwork;     // its URL; a flat neutral square until it loads (never fake art)
};

struct SheetLook
{
    float row_height = 76;
    float header_height = 64;
    float label_size = 30;
    float value_size = 26;
    float subtitle_size = 26;
    float header_size = 26;
    float padding = 20;
    float thumbnail = 80;
    // What the sheet sits on: the scroll cue fades the edge row into it.
    gfx::Color ground = style::kInk;
};

// A ui::ListView showing a window of the rows. ListView scrolls by half a row
// to keep a margin around the focus, which leaves a cut row at the edge; this
// shifts the window by whole rows instead, so every row on screen is whole and
// the view never scrolls at all. Row tags and focus are in the full row list.
class SheetList
{
  public:
    SheetList();
    SheetList(const SheetList &) = delete;
    SheetList &operator=(const SheetList &) = delete;

    SheetLook look;

    void set_bounds(const gfx::Rect &bounds);
    // Replaces the rows; the focus keeps its index, clamped and moved off headers.
    void set_rows(std::vector<SheetRow> rows);
    const std::vector<SheetRow> &rows() const
    {
        return rows_;
    }
    // The focused row, or null when no row can take the focus.
    const SheetRow *focused() const;
    int focus() const
    {
        return focus_;
    }
    // The window: the first row shown and how many rows show.
    int first_shown() const
    {
        return first_;
    }
    int shown_count() const
    {
        return count_;
    }
    void set_focus(int index);
    void focus_first();
    // An inactive list draws no highlight: the other column has the focus.
    void set_active(bool active);
    bool active() const
    {
        return active_;
    }
    void set_reduced_motion(bool reduced);

    ui::Event handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    float height(int index) const;
    int step(int from, int direction) const;
    int settle(int index) const;
    void show(int target);
    void apply();
    void draw_row(ui::Canvas &canvas, const gfx::Rect &row, const SheetRow &meta,
                  float focus) const;
    // k: the window's edge row; direction 1 for rows below, -1 for rows above.
    void draw_cue(ui::Canvas &canvas, int k, float direction) const;

    ui::ListView list_;
    std::vector<SheetRow> rows_;
    gfx::Rect bounds_{0, 0, 600, 400};
    int first_ = 0, count_ = 0, focus_ = 0;
    bool active_ = true;
    std::string signature_;
};
} // namespace hui::afterglow
