// Afterglow - The poster, the track card, the lyric layers and the controls band.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/scenes.hpp"
#include "afterglow/icons.hpp"
#include "afterglow/lyric_overlay.hpp"
#include "afterglow/style.hpp"
#include "ui/glyphs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::afterglow
{
namespace
{
using gfx::Color;
using namespace style;
constexpr float kPi = 3.14159265f;
const Color kAmber = Color::rgb(0xeeb875);
const Color kMint = Color::rgb(0x87cabb);
// LG's night register (player.css #player.night): paper steps down to this,
// secondary text to 72 % of it, the artwork to 72 % opacity and the fields to
// 72 % of their colour.
const Color kNightPaper = Color::rgb(0xc4c0b9);
constexpr float kNightDim = 0.72f;
// Unsung karaoke words sit back at this share of the line's opacity.
constexpr float kUnsung = 0.42f;

bool night(const Presentation &state)
{
    return state.preferences.night_register && state.night_hours;
}
Color paper(const Presentation &state)
{
    return night(state) ? kNightPaper : kPaper;
}
Color muted(const Presentation &state)
{
    return night(state) ? kNightPaper.with_alpha(kNightDim) : kMuted;
}
Color dimmed(Color c, const Presentation &state)
{
    if (!night(state))
        return c;
    return {c.r * kNightDim, c.g * kNightDim, c.b * kNightDim, c.a};
}
Color from(const std::array<float, 3> &rgb)
{
    return Color{rgb[0], rgb[1], rgb[2], 1};
}
std::string clock_text(double seconds)
{
    const int value = static_cast<int>(std::clamp(seconds, 0.0, 86400.0));
    char text[32];
    std::snprintf(text, sizeof(text), "%d:%02d", value / 60, value % 60);
    return text;
}
std::string joined(std::initializer_list<std::string> parts)
{
    std::string out;
    for (const auto &part : parts)
        if (!part.empty())
            out += (out.empty() ? "" : " · ") + part;
    return out;
}
// Rows as ui::paragraph draws them, with the sung bytes in lit and the rest
// in unlit. sung == npos draws every row lit (no word timings).
float karaoke_paragraph(gfx::DrawList &list, const ui::FontRef &font,
                        const ui::ParagraphLines &rows, std::string_view text, std::size_t sung,
                        float x, float y, float size, float line_height, Color lit, Color unlit,
                        gfx::Align align = gfx::Align::left)
{
    if (sung == std::string::npos)
        return ui::paragraph(list, font, rows, x, y, size, line_height, lit, align);
    const auto counts = sung_per_row(rows.lines, text, sung);
    for (std::size_t i = 0; i < rows.lines.size(); ++i)
    {
        const std::string_view row = rows.lines[i];
        const std::size_t split = std::min(counts[i], row.size());
        float left = x;
        if (align == gfx::Align::center)
            left = x - font.measure(row, size) / 2;
        else if (align == gfx::Align::right)
            left = x - font.measure(row, size);
        const auto head = row.substr(0, split);
        if (!head.empty())
            ui::text(list, font, head, left, y, size, lit);
        if (split < row.size())
            ui::text(list, font, row.substr(split), left + font.measure(head, size), y, size,
                     unlit);
        y += line_height;
    }
    return y;
}
const LyricLine *line_at(const LyricView &lyrics, int index)
{
    if (lyrics.lines == nullptr || index < 0 || index >= static_cast<int>(lyrics.lines->size()))
        return nullptr;
    return &(*lyrics.lines)[static_cast<std::size_t>(index)];
}
// The line to sing now: before the first line it is the first line, faded
// and introduced by a countdown; an empty timed line is a rest.
struct Singing
{
    std::string current, next, before;
    // Bytes of current already sung (karaoke), npos without word timings.
    std::size_t sung = std::string::npos;
    float current_alpha = 1;
    float progress = -1; // through the current line, when the next one is timed
};
Singing singing(const LyricView &lyrics)
{
    Singing out;
    if (!lyrics.timed || lyrics.empty())
        return out;
    const int active = lyrics.active;
    if (active < 0)
    {
        const auto *first = line_at(lyrics, 0);
        if (first == nullptr)
            return out;
        out.current = first->text;
        out.current_alpha = 0.45f;
        const double wait = first->seconds - lyrics.now;
        if (wait >= 1)
            out.before = "Starts in " + clock_text(wait);
        if (const auto *next = line_at(lyrics, 1))
            out.next = next->text;
        return out;
    }
    const auto *line = line_at(lyrics, active);
    out.current = line->text.empty() ? "·  ·  ·" : line->text;
    out.current_alpha = line->text.empty() ? 0.5f : 1;
    if (lyrics.karaoke && !line->text.empty())
        out.sung = sung_bytes(*line, lyrics.now);
    if (active > 0)
        out.before = line_at(lyrics, active - 1)->text;
    if (const auto *next = line_at(lyrics, active + 1))
    {
        out.next = next->text;
        if (next->seconds > line->seconds)
            out.progress =
                static_cast<float>((lyrics.now - line->seconds) / (next->seconds - line->seconds));
    }
    return out;
}
float slide(const SceneClock &clock, float distance, float seconds = 0.28f)
{
    if (clock.reduced_motion)
        return 0;
    return (1 - ease_out(clock.lyric_age / seconds)) * distance;
}
float fade_in(const SceneClock &clock, float seconds = 0.28f)
{
    return std::clamp(clock.lyric_age / seconds, 0.0f, 1.0f);
}
// Field B: LG's "Up next" list, the next two songs. One song keeps room for a
// two-line title; two share the field one line each.
void draw_next_field(const Scene &scene, const gfx::Rect &b, Color tb)
{
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    // The drawn song's next two; the Queue sheet's window may belong to another speaker.
    const auto &upcoming = scene.state.companion.next_up;
    const float x = b.x + kFieldInset;
    const float width = b.w - kFieldInset * 2;
    ui::text(list, fonts.semibold, "Up next", x, 725, kTypeBody, tb);
    list.line(x, 744, b.x + b.w - kFieldInset, 744, 2, tb);
    if (upcoming.size() < 2)
    {
        const float end = ui::paragraph(list, fonts.semibold,
                                        upcoming.empty() ? "Nothing queued" : upcoming[0].title, x,
                                        792, 34, width, 38, tb, 2);
        if (!upcoming.empty())
            ui::text(list, fonts.regular,
                     fonts.regular.font->fit(upcoming[0].artist, kTypeMin, width), x, end - 4,
                     kTypeMin, tb);
        return;
    }
    for (std::size_t i = 0; i < 2; ++i)
    {
        const float top = 790 + static_cast<float>(i) * 76;
        ui::text(list, fonts.semibold, fonts.semibold.font->fit(upcoming[i].title, 32, width), x,
                 top, 32, tb);
        ui::text(list, fonts.regular, fonts.regular.font->fit(upcoming[i].artist, kTypeMin, width),
                 x, top + 32, kTypeMin, tb.with_alpha(0.85f));
    }
}
} // namespace

gfx::Color field_a(const Presentation &state)
{
    return dimmed(readable_field(from(state.artwork_color)), state);
}
gfx::Color field_b(const Presentation &state)
{
    const Color a = readable_field(from(state.artwork_color));
    Color b = readable_field(from(state.artwork_secondary));
    // Two fields of one colour read as one block; LG uses a second palette role.
    if (contrast(a, b) < 1.25f)
        b = readable_field({a.r * 0.55f, a.g * 0.55f, a.b * 0.55f, 1});
    return dimmed(b, state);
}
bool night_register(const Presentation &state)
{
    return night(state);
}

IdleCopy idle_copy(const Presentation &state)
{
    // Nothing useful on screen for a while (concepts/afterglow.cpp): offer the check.
    if (state.setup_offer)
        return {"Ready when you are.",
                !state.configured ? "Add your Music Assistant server"
                                  : "Connecting to Music Assistant…",
                "Something not right? Check my setup in Settings."};
    if (!state.configured)
        return {"Ready when you are.", "Add your Music Assistant server",
                "Press Options, then Settings, Account, Sign in."};
    if (!state.connected)
        return {"Ready when you are.", "Connecting to Music Assistant…", state.status};
    return {"Ready when you are.", "Choose Afterglow PS5 in Music Assistant",
            state.companion_configured ? "Or press Options to browse your music." : ""};
}

void draw_visual_style(const Scene &scene, int style)
{
    auto &list = scene.list;
    const auto &spectrum = scene.state.spectrum;
    float energy = 0;
    for (float band : spectrum)
        energy += band / 32;
    // The beat is an accent, not a strobe: a small lift on the bass side, and
    // none at all when motion is reduced.
    const float beat = scene.clock.reduced_motion ? 0 : std::clamp(scene.state.beat, 0.0f, 1.0f);
    if (style == 2)
    {
        for (std::size_t i = 0; i < spectrum.size(); ++i)
        {
            const float low = 1 - static_cast<float>(i) / 31;
            const float height = (4 + spectrum[i] * 390) * (1 + 0.14f * beat * low);
            const float x = 320 + static_cast<float>(i) * 40;
            const Color colour = gfx::mix(kAmber, kMint, static_cast<float>(i) / 31);
            list.rounded_rect({x, 540 - height * 0.5f, 16, height}, 8,
                              gfx::mix(colour, kPaper, 0.35f * beat * low));
        }
        return;
    }
    // The bands deform one continuous halo; movement follows measured audio.
    for (int layer = 0; layer < 3; ++layer)
        for (int i = 0; i < 192; ++i)
        {
            const auto point = [&](int segment)
            {
                const float a = static_cast<float>(segment) * 2 * kPi / 192;
                const float sample =
                    (0.5f + 0.5f * std::sin(a * 2 + scene.clock.time * 0.09f)) * 31;
                const auto lo = static_cast<std::size_t>(sample);
                const float value =
                    spectrum[lo] * (1 - sample + static_cast<float>(lo)) +
                    spectrum[std::min<std::size_t>(31, lo + 1)] * (sample - static_cast<float>(lo));
                const float radius =
                    212 + static_cast<float>(layer) * 22 + 105 * value + 24 * energy + 16 * beat;
                return std::array<float, 2>{960 + std::cos(a) * radius * 1.4f,
                                            540 + std::sin(a) * radius * 0.78f};
            };
            const auto a = point(i);
            const auto b = point(i + 1);
            const auto colour = gfx::mix(
                kAmber, kMint, (std::sin(static_cast<float>(i) * 2 * kPi / 192) + 1) * 0.5f);
            list.line(a[0], a[1], b[0], b[1], layer == 0 ? 3.5f + 2 * beat : 1.5f,
                      colour.with_alpha(layer == 0 ? 0.95f : 0.26f + 0.2f * beat));
        }
}

void draw_slab(const Scene &scene, float ground)
{
    auto &list = scene.list;
    list.rounded_rect({kSlabWidth, 0, 1920 - kSlabWidth, 1080}, 0, kInk.with_alpha(ground));
    // A full-height cover slab, cropped rather than stretched. It overhangs the
    // screen by 2 px: an edge at exactly 0 is antialiased, and the visuals
    // showed through as a coloured line down the left side.
    const gfx::Rect slab{-2, -2, kSlabWidth + 2, 1084};
    list.rounded_rect(slab, 0, field_a(scene.state));
    if (scene.state.artwork_texture)
        list.image(scene.state.artwork_texture, slab, {0.196f, 0, 0.608f, 1},
                   Color::rgb(0xffffff).with_alpha(night(scene.state) ? kNightDim : 1));
}

void draw_poster(const Scene &scene, bool lyrics, float ground)
{
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const auto &state = scene.state;
    const auto &track = state.companion.track;
    const bool idle = state.title.empty();
    const IdleCopy copy = idle_copy(state);
    const Color ink_paper = paper(state);
    const Color soft = muted(state);
    draw_slab(scene, ground);

    // Where the music plays, and what state it is in. The stream's format and
    // quality live in Details, not on the poster (LG 1.5.0).
    const std::string where = state.preview          ? "Listening room · Preview"
                              : !state.group.empty() ? state.group
                                                     : "This PS5";
    const std::string status =
        idle ? (state.connected    ? "Idle"
                : state.configured ? "Connecting…"
                                   : "Not set up")
             : joined({state.playing ? "Playing" : "Paused",
                       lyrics && scene.lyrics.empty()
                           ? (scene.lyrics.retrying  ? "Lyrics unavailable, retrying shortly"
                              : scene.lyrics.failed  ? "Lyrics couldn’t load"
                              : scene.lyrics.loading ? "Finding lyrics…"
                                                     : "No lyrics for this song")
                           : ""});
    const std::string status_line = fonts.regular.font->fit(status, kTypeMin, 720);
    const float status_width = fonts.regular.font->measure(status_line, kTypeMin);
    ui::text(list, fonts.regular,
             fonts.regular.font->fit(where, kTypeMin, std::max(240.0f, 1104 - status_width)),
             kColumn, 92, kTypeMin, soft);
    ui::text(list, fonts.regular, status_line, kSafeRight, 92, kTypeMin, soft, gfx::Align::right);
    list.line(kColumn, 124, kSafeRight, 124, 2, kRule);

    const auto title_block = [&](const std::string &title, float bottom)
    {
        const float size = title.size() <= 12   ? 184.0f
                           : title.size() <= 26 ? 144.0f
                           : title.size() <= 50 ? 104.0f
                                                : 80.0f;
        const auto lines = ui::paragraph_lines(fonts.display, title, size, 1144, 3);
        const float top = bottom - static_cast<float>(lines.lines.size() - 1) * size * 0.94f;
        ui::paragraph(list, fonts.display, lines, kColumn, top, size, size * 0.94f, ink_paper);
    };

    const bool timed_lyrics = lyrics && !idle && scene.lyrics.timed && !scene.lyrics.empty();
    const bool plain_lyrics = lyrics && !idle && !scene.lyrics.timed && !scene.lyrics.empty();
    if (timed_lyrics)
    {
        // LG's lyric stack, bottom-aligned above the fields: the line before,
        // the line now (up to three lines), a karaoke rule, the line after.
        const Singing s = singing(scene.lyrics);
        const float offset = slide(scene.clock, 48);
        const float in = fade_in(scene.clock);
        const float next_baseline = 618 + offset;
        if (!s.next.empty())
            ui::text(list, fonts.regular, fonts.regular.font->fit(s.next, 39, 1144), kColumn,
                     next_baseline, 39, soft.with_alpha(soft.a * in));
        const float rule_y = next_baseline - 62;
        const auto lines = ui::paragraph_lines(fonts.display, s.current, 92, 1144, 3);
        const float last = rule_y - 22;
        const float first = last - static_cast<float>(lines.lines.size() - 1) * 90;
        const Color now = ink_paper.with_alpha(s.current_alpha * (0.35f + 0.65f * in));
        karaoke_paragraph(list, fonts.display, lines, s.current, s.sung, kColumn, first, 92, 90,
                          now, now.with_alpha(now.a * kUnsung));
        if (s.progress >= 0)
        {
            float width = 0;
            for (const auto &line : lines.lines)
                width = std::max(width, fonts.display.font->measure(line, 92));
            list.line(kColumn, rule_y, kColumn + width, rule_y, 4, kRule);
            list.line(kColumn, rule_y, kColumn + width * std::clamp(s.progress, 0.0f, 1.0f), rule_y,
                      4, ink_paper);
        }
        if (!s.before.empty())
            ui::text(list, fonts.regular, fonts.regular.font->fit(s.before, 32, 1144), kColumn,
                     first - 104, 32, soft);
    }
    else if (plain_lyrics)
    {
        // Untimed lyrics are paged by hand; nothing pretends to know the timing.
        // Clear of the toast line; the page height matches paginate() in the screen.
        float y = 248;
        const auto &lines = *scene.lyrics.lines;
        const int end = std::min(static_cast<int>(lines.size()),
                                 scene.lyrics.first + std::max(1, scene.lyrics.count));
        for (int i = scene.lyrics.first; i < end; ++i)
            y = ui::paragraph(list, fonts.semibold, lines[static_cast<std::size_t>(i)].text,
                              kColumn, y, 44, 1144, 52, ink_paper, 2) +
                10;
        char note[64];
        std::snprintf(note, sizeof(note), "Untimed lyrics · page %d of %d", scene.lyrics.page + 1,
                      std::max(1, scene.lyrics.pages));
        ui::text(list, fonts.regular, note, kColumn, 640, kTypeMin, soft);
    }
    else
    {
        title_block(idle ? copy.title : state.title, 608);
    }

    const gfx::Rect a{kColumn, 670, 706, 248};
    const gfx::Rect b{1410, 670, 414, 248};
    const Color fa = field_a(state);
    const Color fb = field_b(state);
    const Color ta = field_text(fa);
    const Color tb = field_text(fb);
    list.rounded_rect(a, 0, fa);
    list.rounded_rect(b, 0, fb);
    const float inset = a.x + kFieldInset;
    if (idle)
    {
        const float end =
            ui::paragraph(list, fonts.semibold, copy.line, inset, 735, 40, 650, 46, ta, 2);
        if (!copy.note.empty())
            ui::paragraph(list, fonts.regular, copy.note, inset, end + 4, kTypeBody, 650, 36,
                          ta.with_alpha(0.85f), 3);
    }
    else if (lyrics)
    {
        const float end =
            ui::paragraph(list, fonts.display, state.title, inset, 735, 43, 650, 46, ta, 2);
        ui::text(list, fonts.semibold, fonts.semibold.font->fit(state.artist, 32, 650), inset,
                 end + 4, 32, ta);
    }
    else
    {
        const float end =
            ui::paragraph(list, fonts.semibold, state.artist, inset, 735, 52, 650, 56, ta, 2);
        ui::text(list, fonts.regular,
                 fonts.regular.font->fit(joined({state.album, track.year}), kTypeBody, 650), inset,
                 end - 56 + 43, kTypeBody, ta);
    }
    draw_next_field(scene, b, tb);

    const double duration = track.duration;
    if (!idle && duration > 0)
    {
        const double elapsed = std::max(0.0, scene.clock.position);
        ui::text(list, fonts.regular, clock_text(elapsed), kColumn, 962, kTypeMin, ink_paper);
        ui::text(list, fonts.regular, clock_text(duration), kSafeRight, 962, kTypeMin, ink_paper,
                 gfx::Align::right);
        list.line(780, 953, 1728, 953, 2, kRule);
        list.line(780, 953,
                  780 + 948 * static_cast<float>(std::clamp(elapsed / duration, 0.0, 1.0)), 953, 2,
                  ink_paper);
    }
}

namespace
{
// What the track card shows: the song, or the idle copy before there is one.
struct CardText
{
    std::string title, artist, meta;
};
CardText card_text(const Presentation &state)
{
    const bool idle = state.title.empty();
    const IdleCopy copy = idle_copy(state);
    const auto &track = state.companion.track;
    return {idle ? copy.title : state.title, idle ? copy.line : state.artist,
            idle ? copy.note : joined({state.album, track.year, track.label})};
}
} // namespace

float track_card_height(const ui::Fonts &fonts, const Presentation &state)
{
    const CardText card = card_text(state);
    const auto lines = ui::paragraph_lines(fonts.display, card.title, 58, 1184, 2);
    const float title_h = static_cast<float>(lines.lines.size()) * 62;
    return 40 + title_h + (card.artist.empty() ? 0 : 44) + (card.meta.empty() ? 0 : 38) + 30;
}

void draw_track_card(const Scene &scene)
{
    if (scene.clock.card < 0.01f)
        return;
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const auto &state = scene.state;
    const CardText card = card_text(state);
    const std::string &title = card.title, &artist = card.artist, &meta = card.meta;
    // LG's card fits its content, up to 62 % of the width.
    const auto lines = ui::paragraph_lines(fonts.display, title, 58, 1184, 2);
    float width = 0;
    for (const auto &line : lines.lines)
        width = std::max(width, fonts.display.font->measure(line, 58));
    width = std::max(width, fonts.semibold.font->measure(artist, 32));
    width = std::max(width, fonts.regular.font->measure(meta, kTypeMin));
    width = std::clamp(width + kFieldInset * 2, 520.0f, 1240.0f);
    const float height = track_card_height(fonts, state);
    const gfx::Rect field{kSafeLeft, 980 - height, width, height};
    const Color fa = field_a(state);
    const Color text = field_text(fa);
    list.push_opacity(scene.clock.card);
    list.rounded_rect(field, 0, fa);
    float y = ui::paragraph(list, fonts.display, lines, field.x + kFieldInset, field.y + 40 + 46,
                            58, 62, text);
    y -= 62;
    if (!artist.empty())
    {
        y += 44;
        ui::text(list, fonts.semibold, fonts.semibold.font->fit(artist, 32, width - 56),
                 field.x + kFieldInset, y, 32, text);
    }
    if (!meta.empty())
    {
        y += 38;
        ui::text(list, fonts.regular, fonts.regular.font->fit(meta, kTypeMin, width - 56),
                 field.x + kFieldInset, y, kTypeMin, text.with_alpha(0.85f));
    }
    list.pop_opacity();
}

namespace
{
// Toasts drop in centred at the top (draw_toast, 140..200 px).
constexpr float kToastBottom = 200;
// Where an overlay may go: under the toast line, so a notice never covers the
// words, above the hints, and a gutter above the track card while it shows.
OverlayRoom overlay_room(const Scene &scene, float bottom)
{
    OverlayRoom room;
    room.top = kToastBottom + kOverlayGutter;
    room.bottom = bottom;
    room.card = scene.clock.card;
    if (room.card > 0)
        room.card_top = 980 - track_card_height(scene.fonts, scene.state);
    return room;
}
// Background None keeps words legible over bright visuals without a band:
// draw(colour) runs first in soft ink on a ring around the words, then the
// caller draws them for real.
// Quieter words over that ring are greyed, not made see-through, so the ring
// does not show through them as a second copy.
// Background Shade: a band of the dark ground across the whole screen width,
// 2.2 times the text block (from its first line's capitals to the lead
// line), clear at its top and bottom and darkest over the words. Vertical
// gradients only, so it has no side edges (as LG's).
void shade_band(gfx::DrawList &list, float block_top, float block_bottom)
{
    const float block = block_bottom - block_top;
    const float ramp = block * 0.6f;
    const Color clear = kInk.with_alpha(0), dark = kInk.with_alpha(0.78f);
    // Wider than the canvas: the OLED drift moves everything a few pixels sideways.
    list.gradient_rect({-16, block_top - ramp, 1952, ramp}, 0, clear, dark);
    list.rounded_rect({-16, block_top, 1952, block}, 0, dark);
    list.gradient_rect({-16, block_bottom, 1952, ramp}, 0, dark, clear);
}
Color over_outline(Color c, float share)
{
    return gfx::mix(kInk, c, share).with_alpha(c.a);
}
template <typename Draw> void soft_outline(gfx::DrawList &list, float spread, Draw &&draw)
{
    constexpr float kRing[8][2] = {{1, 0},  {0.71f, 0.71f},   {0, 1},  {-0.71f, 0.71f},
                                   {-1, 0}, {-0.71f, -0.71f}, {0, -1}, {0.71f, -0.71f}};
    for (const auto &d : kRing)
    {
        list.push_transform(1, 0, 0, d[0] * spread * 1.6f, d[1] * spread * 1.6f);
        draw(kInk.with_alpha(0.07f));
        list.pop_transform();
        list.push_transform(1, 0, 0, d[0] * spread, d[1] * spread);
        draw(kInk.with_alpha(0.26f));
        list.pop_transform();
    }
}
} // namespace

float lyric_line_width(const ui::Fonts &fonts, const LyricView &lyrics, float scale)
{
    const Singing s = singing(lyrics);
    const auto lines = ui::paragraph_lines(fonts.display, s.current, 64 * scale, 1184 * scale, 2);
    float width = fonts.regular.font->measure(s.next.empty() ? s.before : s.next, 34 * scale);
    for (const auto &line : lines.lines)
        width = std::max(width, fonts.display.font->measure(line, 64 * scale));
    return std::clamp(width + kFieldInset * 2 * scale, 480.0f * scale, 1240.0f * scale);
}

void draw_lyric_line(const Scene &scene)
{
    if (scene.clock.lyric < 0.01f)
        return;
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const OverlayLook &look = scene.state.preferences.line_look;
    const float k = overlay_scale(look.size);
    const Singing s = singing(scene.lyrics);
    // Sized to the line; a two-row line grows the field upwards, so the lead
    // line below never moves.
    const float width = std::max(480.0f * k, scene.clock.lyric_width);
    const float inset = kFieldInset * k;
    const float size = 64 * k, step = 66 * k, lead_size = 34 * k;
    const auto lines = ui::paragraph_lines(fonts.display, s.current, size, width - inset * 2, 2);
    const float height = (40 + static_cast<float>(lines.lines.size()) * 66 + 52 + 26) * k;
    const float top = overlay_top(look.position, height, overlay_room(scene, 980));
    const gfx::Rect field{kSafeLeft, top, width, height};
    const bool box = look.background == background_box;
    const Color fa = field_a(scene.state);
    // Off the box the words sit on the dark ground or the visuals: paper.
    const Color text = box ? field_text(fa) : paper(scene.state);
    const Color lead_colour = box ? text.with_alpha(0.72f) : muted(scene.state);
    const float in = fade_in(scene.clock);
    list.push_opacity(scene.clock.lyric);
    if (box)
        list.rounded_rect(field, 0, fa);
    const float last = field.y + height - (30 + 34 + 26) * k;
    const float first =
        last - static_cast<float>(lines.lines.size() - 1) * step + slide(scene.clock, 20 * k);
    const float x = field.x + inset;
    const Color now = text.with_alpha(s.current_alpha * (0.3f + 0.7f * in));
    const std::string lead = s.next.empty() ? s.before : s.next;
    const std::string lead_line =
        lead.empty() ? lead : fonts.regular.font->fit(lead, lead_size, width - inset * 2);
    const float lead_y = field.y + height - 30 * k;
    const auto words = [&](Color lit, Color unlit, Color quiet, std::size_t sung)
    {
        karaoke_paragraph(list, fonts.display, lines, s.current, sung, x, first, size, step, lit,
                          unlit);
        if (!lead_line.empty())
            ui::text(list, fonts.regular, lead_line, x, lead_y, lead_size, quiet);
    };
    if (look.background == background_shade)
        shade_band(list, first - size * 0.74f, lead_y + 10 * k);
    if (look.background == background_none)
        soft_outline(list, 3 * k,
                     [&](Color shade) { words(shade, shade, shade, std::string::npos); });
    if (look.background == background_none)
        words(now, over_outline(now, kUnsung), over_outline(text, 0.7f).with_alpha(in), s.sung);
    else
        words(now, now.with_alpha(now.a * kUnsung), lead_colour.with_alpha(lead_colour.a * in),
              s.sung);
    if (s.progress >= 0)
    {
        const float p = std::clamp(s.progress, 0.0f, 1.0f);
        const float y = field.y + height - 3 * k;
        if (box)
            list.line(field.x, y, field.x + field.w * p, y, 6 * k, text.with_alpha(0.55f));
        else
        {
            // No box edge to run along: the rule spans the words' column.
            const float span = width - inset * 2;
            if (look.background == background_none)
                list.line(x, y + 1, x + span * p, y + 1, 10 * k, kInk.with_alpha(0.3f));
            list.line(x, y, x + span * p, y, 6 * k, text.with_alpha(0.7f));
        }
    }
    list.pop_opacity();
}

void draw_stage(const Scene &scene)
{
    if (scene.clock.lyric < 0.01f)
        return;
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const OverlayLook &look = scene.state.preferences.stage_look;
    const float k = overlay_scale(look.size);
    const Singing s = singing(scene.lyrics);
    // A band across the screen, readable from across the room: a 6 px accent,
    // then 300 px of ground, both scaled with the words.
    const float height = 306 * k;
    const float top = overlay_top(look.position, height, overlay_room(scene, 970));
    list.push_opacity(scene.clock.lyric);
    if (look.background == background_box)
    {
        list.rounded_rect({0, top, 1920, 6 * k}, 0, field_a(scene.state));
        list.rounded_rect({0, top + 6 * k, 1920, 300 * k}, 0, kInk.with_alpha(0.82f));
    }
    // One line at the size's scale; Large may use the whole safe width and,
    // before it wraps, steps back to Medium's one line, so it is never smaller.
    const float wide = k > 1 ? kSafeRight - kSafeLeft : 1600.0f;
    float size = 120 * k;
    auto lines = ui::paragraph_lines(fonts.display, s.current, size, wide, 1);
    if (lines.truncated && k > 1)
    {
        size = 120;
        lines = ui::paragraph_lines(fonts.display, s.current, size, wide, 1);
    }
    if (lines.truncated)
    {
        size = 92 * k;
        lines = ui::paragraph_lines(fonts.display, s.current, size, wide, 2);
    }
    const float in = fade_in(scene.clock);
    const float offset = slide(scene.clock, 28 * k);
    const float first = top + (lines.lines.size() > 1 ? 122 : 176) * k;
    const Color now = paper(scene.state).with_alpha(s.current_alpha * (0.3f + 0.7f * in));
    const std::string lead = s.next.empty() ? s.before : s.next;
    const std::string lead_line = lead.empty() ? lead : fonts.regular.font->fit(lead, 44 * k, wide);
    const Color soft = muted(scene.state);
    const auto words = [&](Color lit, Color unlit, Color quiet, std::size_t sung)
    {
        karaoke_paragraph(list, fonts.display, lines, s.current, sung, 960, first + offset, size,
                          size * 0.98f, lit, unlit, gfx::Align::center);
        if (!lead_line.empty())
            ui::text(list, fonts.regular, lead_line, 960, top + 276 * k + offset, 44 * k, quiet,
                     gfx::Align::center);
    };
    if (look.background == background_shade)
        shade_band(list, first + offset - size * 0.74f, top + (276 + 10) * k + offset);
    if (look.background == background_none)
        soft_outline(list, 4 * k,
                     [&](Color shade) { words(shade, shade, shade, std::string::npos); });
    if (look.background == background_none)
        words(now, over_outline(now, kUnsung),
              over_outline(paper(scene.state), soft.a).with_alpha(in), s.sung);
    else
        words(now, now.with_alpha(now.a * kUnsung), soft.with_alpha(soft.a * in), s.sung);
    if (s.progress >= 0)
    {
        const float p = std::clamp(s.progress, 0.0f, 1.0f);
        const float y = top + 303 * k;
        if (look.background == background_box)
            list.line(0, y, 1920 * p, y, 6 * k, field_a(scene.state));
        else
        {
            // Without the band the rule spans the sung line, centred under it.
            float span = 0;
            for (const auto &line : lines.lines)
                span = std::max(span, fonts.display.font->measure(line, size));
            const float x = 960 - span / 2;
            if (look.background == background_none)
                list.line(x, y + 1, x + span * p, y + 1, 10 * k, kInk.with_alpha(0.3f));
            list.line(x, y, x + span * p, y, 6 * k, paper(scene.state).with_alpha(0.7f));
        }
    }
    list.pop_opacity();
}

void draw_toast(const Scene &scene, bool corner)
{
    const auto &state = scene.state;
    constexpr float kShow = 3.5f;
    if (state.toast.empty() || state.toast_age >= kShow)
        return;
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const float age = state.toast_age;
    const float alpha =
        std::min(std::clamp(age / 0.2f, 0.0f, 1.0f), std::clamp((kShow - age) / 0.3f, 0.0f, 1.0f));
    const float drop = scene.clock.reduced_motion ? 0 : (1 - ease_out(age / 0.25f)) * -16;
    const std::string text = fonts.semibold.font->fit(state.toast, kTypeBody, 1200);
    const float width = fonts.semibold.font->measure(text, kTypeBody) + 64;
    const float top = (corner ? kSafeTop : kToastBottom - 60) + drop;
    const float left = corner ? kSafeRight - width : 960 - width / 2;
    list.push_opacity(alpha);
    list.rounded_rect({left, top, width, 60}, 0, kPaper);
    ui::text(list, fonts.semibold, text, left + width / 2, top + 40, kTypeBody, kInk,
             gfx::Align::center);
    list.pop_opacity();
}

const char *section_name(int section)
{
    constexpr const char *kNames[] = {"Browse", "Now playing", "Queue", "Settings", "Home"};
    return kNames[std::clamp(section, 0, 4)];
}

int section_count(const Presentation &state)
{
    return state.home.configured ? 5 : 4;
}

void draw_transport(const Scene &scene, const Transport &transport)
{
    if (transport.open < 0.01f)
        return;
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const auto &state = scene.state;
    const float t = scene.clock.reduced_motion ? 1 : ease_out(transport.open);
    list.push_opacity(std::min(1.0f, transport.open * 2));

    // The section strip: LG's top navigation, the current section ticked.
    const float top = -108 * (1 - t);
    list.rounded_rect({0, top, 1920, 108}, 0, kInk);
    list.line(0, top + 108, 1920, top + 108, 2, kRule);
    float x = kSafeLeft;
    for (int i = 0; i < section_count(state); ++i)
    {
        const char *name = section_name(i);
        const bool current = i == transport.section;
        const bool focused = transport.row == 0 && i == transport.focus_section;
        const float label = fonts.semibold.font->measure(name, kTypeBody);
        const float width = label + (current ? 40 : 0);
        if (focused)
            list.rounded_rect({x - 18, top + 22, width + 36, 64}, 0, kPaper);
        const Color colour = focused ? kInk : kPaper;
        if (current)
            draw_icon(list, Icon::check, x + 13, top + 62, 26, colour, 3);
        ui::text(list, fonts.semibold, name, x + (current ? 40 : 0), top + 72, kTypeBody, colour);
        x += width + 72;
    }
    const ui::Hint hints[] = {{ui::Button::l1, "Sections", ui::Button::r1},
                              {ui::Button::circle, "Close"}};
    ui::HintLayout layout;
    layout.size = 30;
    layout.text_size = kTypeTransient;
    layout.cy = top + 62;
    layout.item_gap = 32;
    ui::draw_hints(list, fonts, ui::GlyphStyle::mono(kPaper, kInk), hints, 2, kSafeRight, true,
                   layout);

    // The transport band: what is playing, then eight tiles.
    const float band = 810 + 270 * (1 - t);
    list.rounded_rect({0, band, 1920, 1080 - band + 270}, 0, kInk);
    list.line(0, band, 1920, band, 2, kRule);
    const std::string heading =
        state.title.empty() ? idle_copy(state).title
                            : state.title + (state.artist.empty() ? "" : " · " + state.artist);
    ui::text(list, fonts.semibold, fonts.semibold.font->fit(heading, kTypeMin, 1300), kSafeLeft,
             band + 56, kTypeMin, kPaper);
    const double duration = state.companion.track.duration;
    if (duration > 0)
        ui::text(list, fonts.regular,
                 clock_text(scene.clock.position) + " / " + clock_text(duration), kSafeRight,
                 band + 56, kTypeMin, kMuted, gfx::Align::right);
    const bool favourite = state.companion.track.favorite;
    struct TileView
    {
        Icon icon;
        const char *label;
    };
    const TileView tiles[] = {
        {Icon::previous, "Previous"},
        {state.playing ? Icon::pause : Icon::play, state.playing ? "Pause" : "Play"},
        {Icon::next, "Next"},
        {Icon::back10, "Back 10 s"},
        {Icon::ahead10, "Ahead 10 s"},
        {favourite ? Icon::heart_filled : Icon::heart, favourite ? "Favourited" : "Favourite"},
        {Icon::details, "Details"},
        {Icon::display, "Display"},
    };
    for (int i = 0; i < static_cast<int>(Tile::count); ++i)
    {
        const float tx = kSafeLeft + static_cast<float>(i) * (192 + 24);
        const float ty = band + 82;
        const bool focused = transport.row == 1 && i == transport.tile;
        if (focused)
            list.rounded_rect({tx, ty, 192, 119}, 0, kPaper);
        else
            list.line(tx, ty, tx + 192, ty, 2, kRule);
        const Color colour = focused ? kInk : kPaper;
        draw_icon(list, tiles[i].icon, tx + 40, ty + 40, 40, colour, 3);
        ui::text(list, fonts.regular, tiles[i].label, tx + 20, ty + 98, kTypeMin, colour);
    }
    list.pop_opacity();
}
} // namespace hui::afterglow

namespace hui::afterglow
{
namespace
{
using gfx::Color;
using namespace style;

// LG's idle strip: square tiles, the focused one inverted to paper; the words
// (Browse, Try again) take the width they need at the smallest type size.
constexpr float kIdleTile = 88;
constexpr float kIdleTileGap = 16;
constexpr float kIdleStripY = 936;
const char *idle_word(IdleTileKind kind)
{
    return kind == IdleTileKind::retry   ? "Try again"
           : kind == IdleTileKind::setup ? "Check setup"
                                         : "Browse";
}
float idle_tile_width(const Scene &scene, const IdleTile &tile)
{
    if (tile.kind == IdleTileKind::resume || tile.kind == IdleTileKind::play)
        return kIdleTile;
    const char *word = idle_word(tile.kind);
    return std::max(kIdleTile, scene.fonts.semibold.font->measure(word, kTypeMin) + 24);
}
void draw_idle_strip(const Scene &scene, const IdleView &idle, Color fill_b)
{
    auto &list = scene.list;
    const auto &tiles = *idle.tiles;
    const Color ink_paper = paper(scene.state);
    float x = kColumn;
    for (std::size_t i = 0; i < tiles.size(); ++i)
    {
        const auto &tile = tiles[i];
        const float width = idle_tile_width(scene, tile);
        if (x + width > kSafeRight)
            break;
        const gfx::Rect box{x, kIdleStripY, width, kIdleTile};
        const bool focused = static_cast<int>(i) == idle.focus;
        if (focused)
            list.rounded_rect(box, 0, ink_paper);
        const gfx::Rect inner{box.x + 5, box.y + 5, box.w - 10, box.h - 10};
        if (tile.kind == IdleTileKind::resume || tile.kind == IdleTileKind::play)
        {
            const auto &source = scene.state.thumbnail;
            const std::uint32_t texture =
                source && !tile.item.artwork.empty() ? source(tile.item.artwork) : 0;
            list.rounded_rect(inner, 0, fill_b);
            if (texture != 0)
                list.image(texture, inner, gfx::kFullUv,
                           Color{1, 1, 1, night(scene.state) ? kNightDim : 1.0f});
        }
        else
        {
            const Color colour = focused ? kInk : ink_paper;
            if (!focused)
                list.line(box.x, box.y + 1, box.x + box.w, box.y + 1, 2, kRule);
            ui::text(list, scene.fonts.semibold, idle_word(tile.kind), box.x + 12,
                     box.y + box.h - 16, kTypeMin, colour);
        }
        x += width + kIdleTileGap;
    }
}
} // namespace

void draw_idle_poster(const Scene &scene, const IdleView &idle)
{
    if (idle.tiles == nullptr || idle.tiles->empty())
        return;
    auto &list = scene.list;
    const auto &fonts = scene.fonts;
    const auto &state = scene.state;
    const auto &tiles = *idle.tiles;
    const auto &tile = tiles[static_cast<std::size_t>(
        std::clamp(idle.focus, 0, static_cast<int>(tiles.size()) - 1))];
    const Color ink_paper = paper(state);
    const Color soft = muted(state);
    // Only the paused song has colours of its own (LG #idle.neutral); the
    // last song's fields never dress another item.
    const bool own = tile.kind == IdleTileKind::resume;
    const Color fa = own ? field_a(state) : dimmed(kNeutralA, state);
    const Color fb = own ? field_b(state) : dimmed(kNeutralB, state);
    list.rounded_rect({0, 0, 1920, 1080}, 0, kInk);

    const std::string status_line = fonts.regular.font->fit(idle.status, kTypeMin, 520);
    const float status_width = fonts.regular.font->measure(status_line, kTypeMin);
    ui::text(list, fonts.regular,
             fonts.regular.font->fit(idle.where, kTypeMin, std::max(240.0f, 1104 - status_width)),
             kColumn, 92, kTypeMin, soft);
    ui::text(list, fonts.regular, status_line, kSafeRight, 92, kTypeMin, soft, gfx::Align::right);
    list.line(kColumn, 124, kSafeRight, 124, 2, kRule);

    // The cover in a square box (LG .idle-art), backed by field B while it loads.
    const gfx::Rect art{kSafeLeft, 173, 544, 544};
    if (tile.kind == IdleTileKind::resume || tile.kind == IdleTileKind::play)
    {
        list.rounded_rect(art, 0, fb);
        if (state.artwork_texture != 0 && !tile.item.artwork.empty())
            list.image(state.artwork_texture, art, gfx::kFullUv,
                       Color{1, 1, 1, night(state) ? kNightDim : 1.0f});
    }

    const std::string &title = idle.text.title;
    const float size = title.size() <= 12   ? 184.0f
                       : title.size() <= 26 ? 144.0f
                       : title.size() <= 50 ? 104.0f
                                            : 80.0f;
    const auto lines = ui::paragraph_lines(fonts.display, title, size, 1144, 3);
    const float top = 608 - static_cast<float>(lines.lines.size() - 1) * size * 0.94f;
    ui::paragraph(list, fonts.display, lines, kColumn, top, size, size * 0.94f, ink_paper);

    const gfx::Rect field{kColumn, 670, 706, 248};
    // Night fields carry the night paper (LG 1.4.4), never full paper.
    const Color ta = contrast(fa, kInk) >= contrast(fa, kPaper) ? kInk : ink_paper;
    list.rounded_rect(field, 0, fa);
    const float inset = field.x + kFieldInset;
    const float end = ui::paragraph(list, fonts.semibold, idle.text.detail, inset, 735, 40,
                                    field.w - kFieldInset * 2, 46, ta, 2);
    const float hint_y = std::max(end + 22, 850.0f);
    if (!idle.note.empty())
        ui::text(list, fonts.semibold,
                 fonts.semibold.font->fit(idle.note, kTypeBody, field.w - kFieldInset * 2), inset,
                 hint_y + 10, kTypeBody, ta);
    else
    {
        const ui::Hint hint[] = {{ui::Button::cross, idle.text.hint.c_str()}};
        ui::HintLayout layout;
        layout.size = 34;
        layout.text_size = kTypeBody;
        layout.cy = hint_y;
        ui::draw_hints(list, fonts, ui::GlyphStyle::mono(ta, fa), hint, 1, inset, false, layout);
    }

    draw_idle_strip(scene, idle, fb);
    // The dim register: a veil of the ground, so nothing overlapping shows through.
    if (idle.level < 0.999f)
        list.rounded_rect({0, 0, 1920, 1080}, 0, kInk.with_alpha(1 - idle.level));
}
} // namespace hui::afterglow
