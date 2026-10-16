// Afterglow - Tonight's programme drawn as a concert programme on the idle poster.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/programme_scene.hpp"
#include "afterglow/icons.hpp"
#include "afterglow/style.hpp"
#include "ui/glyphs.hpp"
#include <algorithm>

namespace hui::afterglow
{
namespace
{
using gfx::Color;
using namespace style;

// The idle poster's top line and hairline, so the page reads as its sibling.
constexpr float kTopBaseline = 92;
constexpr float kTopRule = 124;
// Columns 1-4 end where the poster's slab does; columns 5-12 hold the songs.
constexpr float kHeroWidth = kSlabWidth - kSafeLeft;
constexpr float kHeroSize = 144;
constexpr float kHeroBaseline = 262;
constexpr float kDateSize = 56;
constexpr float kDateBaseline = 340;
constexpr float kListTop = 148;
constexpr float kRow = 70;
constexpr float kGutter = 24;
constexpr float kListColumn = (kSafeRight - kColumn - kGutter) / 2;
constexpr float kNumberWidth = 56;
constexpr float kAction = 80;
constexpr float kActionsTop = kListTop + kRow * 11 - kAction * 2;
constexpr float kHintY = 976;

void draw_row(const Scene &scene, const ProgrammeRow &row, float x, float y, Color ink, Color soft)
{
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const float cy = y + kRow / 2;
    list.rounded_rect({x, y + kRow - 2, kListColumn, 2}, 0, kRule);
    if (!row.number.empty())
        ui::text(list, fonts.regular, row.number, x, cy + 9, kTypeMin, soft);
    const float left = x + kNumberWidth;
    float right = x + kListColumn;
    if (!row.time.empty())
    {
        ui::text(list, fonts.regular, row.time, right, cy + 9, kTypeMin, soft, gfx::Align::right);
        right -= fonts.regular.measure(row.time, kTypeMin) + 20;
    }
    const float width = right - left;
    const auto &title_font = row.more ? fonts.regular : fonts.semibold;
    const std::string title = title_font.font->fit(row.title, kTypeBody, width);
    if (row.artist.empty())
    {
        ui::text(list, title_font, title, left, cy + 10, kTypeBody, row.more ? soft : ink);
        return;
    }
    ui::text(list, title_font, title, left, y + 31, kTypeBody, ink);
    ui::text(list, fonts.regular, fonts.regular.font->fit(row.artist, kTypeMin, width), left,
             y + 60, kTypeMin, soft);
}

// An action row: focus inverts paper on ink; a success wears the drawn tick.
void draw_action(const Scene &scene, float y, const char *label, const std::string &value,
                 bool tick, bool focused, Color ink)
{
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const gfx::Rect box{kSafeLeft, y, kHeroWidth, kAction};
    if (focused)
        list.rounded_rect(box, 0, ink);
    list.rounded_rect({box.x, y + kAction - 2, box.w, 2}, 0, kRule);
    const Color text = focused ? kInk : ink;
    float left = box.x + 20;
    const float baseline = value.empty() ? y + 51 : y + 36;
    if (tick)
    {
        draw_icon(list, Icon::check, left + 12, baseline - 10, 26, text, 3);
        left += 38;
    }
    ui::text(list, fonts.semibold, label, left, baseline, kTypeLabel, text);
    if (!value.empty())
        ui::text(list, fonts.regular,
                 fonts.regular.font->fit(value, kTypeMin, box.x + box.w - 20 - (box.x + 20)),
                 box.x + 20, y + 66, kTypeMin, focused ? kInk : text.with_alpha(text.a * 0.68f));
}
} // namespace

void draw_programme(const Scene &scene, const ProgrammePage &page, float level)
{
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const Color ink = poster_paper(scene.state);
    const Color soft = poster_muted(scene.state);
    list.rounded_rect({0, 0, 1920, 1080}, 0, kInk);

    const float summary_width = fonts.regular.measure(page.summary, kTypeMin);
    ui::text(list, fonts.regular,
             fonts.regular.font->fit(page.where, kTypeMin,
                                     std::max(240.0f, kSafeRight - kSafeLeft - summary_width - 48)),
             kSafeLeft, kTopBaseline, kTypeMin, soft);
    ui::text(list, fonts.regular, page.summary, kSafeRight, kTopBaseline, kTypeMin, soft,
             gfx::Align::right);
    list.line(kSafeLeft, kTopRule, kSafeRight, kTopRule, 2, kRule);

    // The headline word at display size, smaller only if it would leave columns 1-4.
    const float word_width = fonts.display.measure(page.headline.word, kHeroSize);
    const float size = word_width > kHeroWidth ? kHeroSize * kHeroWidth / word_width : kHeroSize;
    ui::text(list, fonts.display, page.headline.word, kSafeLeft, kHeroBaseline, size, ink);
    ui::text(list, fonts.display,
             fonts.display.font->fit(page.headline.date, kDateSize, kHeroWidth), kSafeLeft,
             kDateBaseline, kDateSize, ink);

    for (std::size_t c = 0; c < page.columns.size(); ++c)
    {
        const float x = kColumn + static_cast<float>(c) * (kListColumn + kGutter);
        float y = kListTop;
        for (const auto &row : page.columns[c])
        {
            draw_row(scene, row, x, y, ink, soft);
            y += kRow;
        }
    }

    list.rounded_rect({kSafeLeft, kActionsTop - 2, kHeroWidth, 2}, 0, kRule);
    draw_action(scene, kActionsTop, "Keep it as a playlist", page.keep_value, page.keep_tick,
                page.focus == 0, ink);
    draw_action(scene, kActionsTop + kAction, "Back", {}, false, page.focus == 1, ink);
    const ui::Hint hints[] = {{ui::Button::cross, "Select"}, {ui::Button::circle, "Back"}};
    ui::HintLayout layout;
    layout.size = 34;
    layout.text_size = kTypeTransient;
    layout.cy = kHintY;
    layout.item_gap = 40;
    ui::draw_hints(list, fonts, ui::GlyphStyle::mono(ink, kInk), hints, 2, kSafeLeft, false,
                   layout);

    // The dim register: a veil of the ground, as on the idle poster.
    if (level < 0.999f)
        list.rounded_rect({0, 0, 1920, 1080}, 0, kInk.with_alpha(1 - level));
}
} // namespace hui::afterglow
