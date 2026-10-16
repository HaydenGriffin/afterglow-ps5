// Afterglow - Hairline-ruled sheet rows that page by whole rows, never slivers.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/sheet_list.hpp"
#include "afterglow/icons.hpp"
#include "afterglow/presentation.hpp"
#include "afterglow/style.hpp"
#include <algorithm>

namespace hui::afterglow
{
namespace
{
// Room for the scroll bar beside the rows when the window hides some: the
// rows narrow instead of the bar sitting outside the title-safe area.
constexpr float kThumbRoom = 16;
constexpr float kThumbWidth = 6;

// Stroked chevrons: the Archivo faces have no single angle quotes.
void chevron(gfx::DrawList &list, float tip_x, float cy, float direction, gfx::Color colour)
{
    const float back = tip_x - 8.0f * direction;
    list.line(back, cy - 8, tip_x, cy, 2.5f, colour);
    list.line(tip_x, cy, back, cy + 8, 2.5f, colour);
}
} // namespace

SheetList::SheetList()
{
    auto &t = list_.style.theme;
    t.style = ui::SurfaceStyle::flat;
    t.page = t.surface = style::kInk;
    t.surface_high = style::kDisabledFill;
    t.text = style::kPaper;
    t.text_muted = style::kMuted;
    t.primary = t.accent = t.focus = style::kPaper;
    t.on_primary = style::kInk;
    t.outline = style::kPaper.with_alpha(0);
    list_.style.highlight.kind = ui::HighlightKind::fill;
    list_.style.highlight.radius = 0;
    list_.style.highlight.breathe = false;
    list_.style.highlight.color = style::kPaper;
    list_.style.focus_shift = 0;
    list_.style.gap = 0;
    list_.style.padding = look.padding;
    list_.style.entrance_step = 0;
    list_.style.edge_fade = 0;
    list_.style.scroll_thumb = false;
    list_.content = [this](ui::Canvas &canvas, const gfx::Rect &row, const ui::ListItem &,
                           int index, float focus)
    {
        const auto at = static_cast<std::size_t>(first_ + index);
        if (at < rows_.size())
            draw_row(canvas, row, rows_[at], active_ ? focus : 0.0f);
    };
}

float SheetList::height(int index) const
{
    return rows_[static_cast<std::size_t>(index)].item.header ? look.header_height
                                                              : look.row_height;
}

int SheetList::step(int from, int direction) const
{
    const int count = static_cast<int>(rows_.size());
    for (int i = from + direction; i >= 0 && i < count; i += direction)
        if (!rows_[static_cast<std::size_t>(i)].item.header)
            return i;
    return -1;
}

// The nearest row that can hold the focus, preferring the one below a header.
int SheetList::settle(int index) const
{
    const int count = static_cast<int>(rows_.size());
    if (count == 0)
        return 0;
    index = std::clamp(index, 0, count - 1);
    if (!rows_[static_cast<std::size_t>(index)].item.header)
        return index;
    const int below = step(index, 1);
    if (below >= 0)
        return below;
    const int above = step(index, -1);
    return above >= 0 ? above : index;
}

// Moves the window the least distance that shows the target row whole, with
// the group heading right above it.
void SheetList::show(int target)
{
    const int count = static_cast<int>(rows_.size());
    if (count == 0)
    {
        first_ = count_ = 0;
        return;
    }
    const float room = bounds_.h;
    target = std::clamp(target, 0, count - 1);
    const int top =
        target > 0 && rows_[static_cast<std::size_t>(target - 1)].item.header ? target - 1 : target;
    first_ = std::clamp(first_, 0, count - 1);
    if (top < first_)
        first_ = top;
    float span = 0;
    for (int i = first_; i <= target; ++i)
        span += height(i);
    while (span > room && first_ < top)
        span -= height(first_++);
    // Never leave empty space under the last row while rows above are hidden.
    float rest = 0;
    for (int i = first_; i < count; ++i)
        rest += height(i);
    while (first_ > 0 && rest + height(first_ - 1) <= room)
        rest += height(--first_);
    float used = 0;
    count_ = 0;
    for (int i = first_; i < count && used + height(i) <= room; ++i, ++count_)
        used += height(i);
    count_ = std::max(count_, 1);
    // A heading at the very bottom would introduce rows that are not shown.
    while (count_ > 1 && rows_[static_cast<std::size_t>(first_ + count_ - 1)].item.header)
        --count_;
}

// Hands the window to the list view. Only the row kinds and heights matter to
// it (the content slot draws the words from rows_), so it is reloaded, which
// snaps its highlight, only when those change.
void SheetList::apply()
{
    const int count = static_cast<int>(rows_.size());
    const bool overflow = first_ > 0 || first_ + count_ < count;
    gfx::Rect rect = bounds_;
    if (overflow)
        rect.w -= kThumbRoom;
    rect.h = 0;
    std::string signature;
    signature.reserve(static_cast<std::size_t>(count_) + 48);
    signature += std::to_string(static_cast<int>(rect.x)) + "," +
                 std::to_string(static_cast<int>(rect.y)) + "," +
                 std::to_string(static_cast<int>(rect.w)) + "," +
                 std::to_string(static_cast<int>(look.row_height)) + "," +
                 std::to_string(static_cast<int>(look.header_height)) + ":";
    for (int i = first_; i < first_ + count_; ++i)
    {
        const auto &item = rows_[static_cast<std::size_t>(i)].item;
        signature += item.header ? 'h' : item.disabled ? 'd' : 'e';
        rect.h += height(i);
    }
    if (signature != signature_)
    {
        std::vector<ui::ListItem> items;
        items.reserve(static_cast<std::size_t>(count_));
        for (int i = first_; i < first_ + count_; ++i)
        {
            ui::ListItem item;
            item.header = rows_[static_cast<std::size_t>(i)].item.header;
            item.disabled = rows_[static_cast<std::size_t>(i)].item.disabled;
            items.push_back(std::move(item));
        }
        list_.style.row_height = look.row_height;
        list_.style.header_height = look.header_height;
        list_.style.padding = look.padding;
        list_.set_bounds(rect);
        list_.set_items(std::move(items));
        signature_ = std::move(signature);
    }
    const int local = focus_ - first_;
    if (count_ > 0 && local >= 0 && local < count_ && list_.focus() != local)
        list_.set_focus(local, true);
}

void SheetList::set_bounds(const gfx::Rect &bounds)
{
    if (bounds.x == bounds_.x && bounds.y == bounds_.y && bounds.w == bounds_.w &&
        bounds.h == bounds_.h)
        return;
    bounds_ = bounds;
    show(focus_);
    apply();
}

void SheetList::set_rows(std::vector<SheetRow> rows)
{
    rows_ = std::move(rows);
    focus_ = settle(focus_);
    show(focus_);
    apply();
}

const SheetRow *SheetList::focused() const
{
    if (rows_.empty())
        return nullptr;
    const auto &row = rows_[static_cast<std::size_t>(focus_)];
    return row.item.header ? nullptr : &row;
}

void SheetList::set_focus(int index)
{
    focus_ = settle(index);
    show(focus_);
    apply();
}

void SheetList::focus_first()
{
    first_ = 0;
    set_focus(0);
}

void SheetList::set_active(bool active)
{
    active_ = active;
    list_.set_active(active);
    list_.style.highlight.kind = active ? ui::HighlightKind::fill : ui::HighlightKind::none;
}

void SheetList::set_reduced_motion(bool reduced)
{
    list_.style.reduced_motion = reduced;
}

ui::Event SheetList::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (focused() == nullptr)
        return ui::Event::none;
    // Circle belongs to the overlay, which calls Panel::back().
    InputFrame frame = input;
    frame.pressed &= ~action_bit(Action::back);
    if (frame.nav == Direction::up || frame.nav == Direction::down)
    {
        const int next = step(focus_, frame.nav == Direction::down ? 1 : -1);
        if (next >= 0)
        {
            const bool heading = next > 0 && rows_[static_cast<std::size_t>(next - 1)].item.header;
            if ((heading ? next - 1 : next) < first_ || next >= first_ + count_)
            {
                show(next);
                if (focus_ < first_ || focus_ >= first_ + count_)
                {
                    // A view shorter than two rows: no glide to keep.
                    focus_ = next;
                    apply();
                    feedback.play(audio::Cue::focus);
                    return ui::Event::moved;
                }
                apply();
            }
        }
        const ui::Event event = list_.handle(frame, feedback);
        focus_ = first_ + list_.focus();
        return event;
    }
    return list_.handle(frame, feedback);
}

void SheetList::update(float dt)
{
    const SheetRow *row = focused();
    // A focused row that refuses Cross steps back to the disabled fill, so its
    // paper text keeps its contrast.
    list_.style.highlight.color =
        row != nullptr && row->item.disabled ? style::kDisabledFill : style::kPaper;
    list_.update(dt);
}

void SheetList::draw_row(ui::Canvas &canvas, const gfx::Rect &row, const SheetRow &meta,
                         float focus) const
{
    auto &list = canvas.list;
    const auto &fonts = canvas.fonts;
    const auto &item = meta.item;
    const gfx::Color on = item.disabled ? style::kPaper : style::kInk;
    const gfx::Color ink = gfx::mix(item.disabled ? style::kFaint : style::kPaper, on, focus);
    const gfx::Color quiet = gfx::mix(item.disabled ? style::kFaint : style::kMuted, on, focus);
    const float cy = row.cy();
    list.rounded_rect({row.x, row.y + row.h - 2, row.w, 2}, 0, style::kRule);

    float left = row.x + look.padding;
    float right = row.x + row.w - look.padding;
    if (meta.thumbnail)
    {
        const gfx::Rect square{row.x, cy - look.thumbnail * 0.5f, look.thumbnail, look.thumbnail};
        const auto &source = presentation().thumbnail;
        const std::uint32_t texture = source && !meta.artwork.empty() ? source(meta.artwork) : 0;
        if (texture != 0)
            list.image(texture, square, gfx::kFullUv, gfx::Color{1, 1, 1, 1});
        else
            list.rounded_rect(square, 0, style::kNeutralA);
        left = row.x + look.thumbnail + 24;
    }
    if (meta.chosen)
    {
        draw_icon(list, Icon::check, left + 12, cy, 26, ink, 3);
        left += 38;
    }
    if (item.chevron)
    {
        chevron(list, right, cy, 1, quiet);
        right -= 30;
    }
    if (!item.value.empty())
    {
        const bool arrows = meta.adjustable && !item.disabled && focus > 0.01f;
        const auto arrow = ink.with_alpha(ink.a * std::min(focus, 1.0f));
        if (arrows)
        {
            chevron(list, right, cy, 1, arrow);
            right -= 24;
        }
        const std::string value =
            fonts.regular.font->fit(item.value, look.value_size, row.w * 0.5f);
        const float width = fonts.regular.measure(value, look.value_size);
        ui::text(list, fonts.regular, value, right, cy + look.value_size * 0.34f, look.value_size,
                 ink, gfx::Align::right);
        right -= width;
        if (arrows)
        {
            chevron(list, right - 16, cy, -1, arrow);
            right -= 24;
        }
        right -= 24;
    }

    const float room = std::max(right - left, 40.0f);
    const auto &label = fonts.semibold;
    if (item.subtitle.empty())
    {
        ui::text(list, label, label.font->fit(item.title, look.label_size, room), left,
                 cy + look.label_size * 0.34f, look.label_size, ink);
        return;
    }
    const float block = look.label_size + look.subtitle_size * 1.25f;
    const float first = cy - block * 0.5f + look.label_size * 0.84f;
    ui::text(list, label, label.font->fit(item.title, look.label_size, room), left, first,
             look.label_size, ink);
    ui::text(list, fonts.regular, fonts.regular.font->fit(item.subtitle, look.subtitle_size, room),
             left, first + look.subtitle_size * 1.3f, look.subtitle_size, quiet);
}

void SheetList::draw(ui::Canvas &canvas) const
{
    if (rows_.empty())
        return;
    list_.draw(canvas);
    auto &list = canvas.list;
    const auto &heading = canvas.fonts.semibold;
    for (int k = 0; k < count_; ++k)
    {
        const auto &item = rows_[static_cast<std::size_t>(first_ + k)].item;
        if (!item.header)
            continue;
        const gfx::Rect r = list_.row_rect(k);
        ui::text(list, heading, heading.font->fit(item.title, look.header_size, r.w), r.x,
                 r.y + r.h - 14, look.header_size, style::kMuted);
    }
    const int count = static_cast<int>(rows_.size());
    if (first_ == 0 && first_ + count_ >= count)
        return;
    // Where the window sits in the whole list, measured in rows.
    float shown = 0;
    for (int k = 0; k < count_; ++k)
        shown += height(first_ + k);
    const float x = bounds_.x + bounds_.w - kThumbWidth;
    const float share = static_cast<float>(count_) / static_cast<float>(count);
    const float size = std::max(shown * share, 36.0f);
    const float at = static_cast<float>(first_) / static_cast<float>(std::max(count - count_, 1));
    list.rounded_rect({x, bounds_.y, kThumbWidth, shown}, 0, style::kRule);
    list.rounded_rect(
        {x, bounds_.y + (shown - size) * std::clamp(at, 0.0f, 1.0f), kThumbWidth, size}, 0,
        style::kMuted);
}
} // namespace hui::afterglow
