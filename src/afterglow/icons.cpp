// Afterglow - Stroked line icons for the transport band and sheets.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/icons.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>

namespace hui::afterglow
{
namespace
{
constexpr float kTau = 6.2831853f;

// Every icon is authored in a unit box of [-1, 1] (y down), like a 24 px SVG
// grid. The pen maps it so the outer edge of the stroke lands at 80% of the
// square, which keeps the ~10% optical padding whatever the stroke weight.
//
// Joins: DrawList::line is a capsule, so two segments sharing an end point
// overlap in a disc of the stroke's radius, which is exactly a round join.
// Arcs end in half discs too. The overlap blends twice, so a translucent
// colour shows slightly denser joints; the band draws icons opaque.
struct Pen
{
    gfx::DrawList &list;
    float cx;
    float cy;
    float unit;
    float stroke;
    gfx::Color colour;

    float x(float u) const
    {
        return cx + u * unit;
    }
    float y(float v) const
    {
        return cy + v * unit;
    }
    void line(float x1, float y1, float x2, float y2) const
    {
        list.line(x(x1), y(y1), x(x2), y(y2), stroke, colour);
    }
    void polyline(std::initializer_list<float> xy, bool closed = false) const
    {
        const float *p = xy.begin();
        const std::size_t points = xy.size() / 2;
        for (std::size_t i = 0; i + 1 < points; ++i)
            line(p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3]);
        if (closed && points > 2)
            line(p[2 * points - 2], p[2 * points - 1], p[0], p[1]);
    }
    // A stroke centred on the circle of radius r: DrawList::arc takes the
    // outer edge, so the half stroke is added back.
    void arc(float ox, float oy, float r, float start, float sweep) const
    {
        list.arc(x(ox), y(oy), r * unit + stroke * 0.5f, stroke, start, sweep, colour, true);
    }
    void dot(float ox, float oy, float r) const
    {
        list.circle(x(ox), y(oy), r * unit, colour);
    }
};

// Two lobes that overlap a little, so the top meets in a soft cusp, and two
// straight sides tangent to the lobes running down to the point.
struct HeartShape
{
    static constexpr float lobe_x = 0.42f;
    static constexpr float lobe_y = -0.36f;
    static constexpr float radius = 0.5f;
    static constexpr float tip_y = 0.9f;
    float cusp_angle = 0.0f;    // where the left lobe meets x = 0, clockwise from 12
    float tangent_angle = 0.0f; // where the left side leaves the left lobe
    float tangent_x = 0.0f;
    float tangent_y = 0.0f;
};

HeartShape heart_shape()
{
    HeartShape h;
    h.cusp_angle = std::asin(HeartShape::lobe_x / HeartShape::radius);
    // Outer tangent from the tip to the left lobe: the angle at the lobe's
    // centre between the tip and the tangent point is acos(r / d).
    const float dx = HeartShape::lobe_x; // the tip is at x = 0
    const float dy = HeartShape::tip_y - HeartShape::lobe_y;
    const float d = std::sqrt(dx * dx + dy * dy);
    const float to_tip = std::atan2(dx, -dy); // clockwise from 12
    h.tangent_angle = to_tip + std::acos(HeartShape::radius / d);
    h.tangent_x = -HeartShape::lobe_x + HeartShape::radius * std::sin(h.tangent_angle);
    h.tangent_y = HeartShape::lobe_y - HeartShape::radius * std::cos(h.tangent_angle);
    return h;
}

void heart_outline(const Pen &pen, const HeartShape &h)
{
    const float sweep = kTau - h.tangent_angle + h.cusp_angle;
    pen.arc(-HeartShape::lobe_x, HeartShape::lobe_y, HeartShape::radius, h.tangent_angle, sweep);
    pen.arc(HeartShape::lobe_x, HeartShape::lobe_y, HeartShape::radius, -h.cusp_angle, sweep);
    pen.line(h.tangent_x, h.tangent_y, 0.0f, HeartShape::tip_y);
    pen.line(-h.tangent_x, h.tangent_y, 0.0f, HeartShape::tip_y);
}

// The polygon runs along the stroke's centre line; the outline drawn over it
// supplies the anti-aliased edge the mesh lacks.
void heart_fill(const Pen &pen, const HeartShape &h)
{
    constexpr int kSteps = 14;
    std::array<float, 2 * (2 * (kSteps + 1) + 1)> xy{};
    int n = 0;
    const auto push = [&](float u, float v)
    {
        xy[static_cast<std::size_t>(2 * n)] = pen.x(u);
        xy[static_cast<std::size_t>(2 * n + 1)] = pen.y(v);
        ++n;
    };
    push(0.0f, HeartShape::tip_y);
    const float sweep = kTau - h.tangent_angle + h.cusp_angle;
    for (int i = 0; i <= kSteps; ++i)
    {
        const float a = h.tangent_angle + sweep * static_cast<float>(i) / kSteps;
        push(-HeartShape::lobe_x + HeartShape::radius * std::sin(a),
             HeartShape::lobe_y - HeartShape::radius * std::cos(a));
    }
    for (int i = 0; i <= kSteps; ++i)
    {
        const float a = -h.cusp_angle + sweep * static_cast<float>(i) / kSteps;
        push(HeartShape::lobe_x + HeartShape::radius * std::sin(a),
             HeartShape::lobe_y - HeartShape::radius * std::cos(a));
    }
    pen.list.polygon(xy.data(), n, pen.colour);
}

// Triangles share one height so previous, play and next sit on the same
// band; play's tip and back edge are nudged right of the box centre because
// a triangle's mass sits towards its flat side.
void play(const Pen &pen)
{
    pen.polyline({-0.62f, -0.86f, 0.88f, 0.0f, -0.62f, 0.86f}, true);
}

void previous(const Pen &pen)
{
    pen.line(-0.86f, -0.86f, -0.86f, 0.86f);
    pen.polyline({0.86f, -0.86f, -0.44f, 0.0f, 0.86f, 0.86f}, true);
}

void next(const Pen &pen)
{
    pen.line(0.86f, -0.86f, 0.86f, 0.86f);
    pen.polyline({-0.86f, -0.86f, 0.44f, 0.0f, -0.86f, 0.86f}, true);
}

void pause(const Pen &pen)
{
    pen.line(-0.42f, -0.86f, -0.42f, 0.86f);
    pen.line(0.42f, -0.86f, 0.42f, 0.86f);
}

void chevrons(const Pen &pen, float direction)
{
    const float d = direction;
    pen.polyline({-0.2f * d, -0.75f, -1.0f * d, 0.0f, -0.2f * d, 0.75f});
    pen.polyline({1.0f * d, -0.75f, 0.2f * d, 0.0f, 1.0f * d, 0.75f});
}

void details(const Pen &pen)
{
    pen.line(-1.0f, -0.81f, 1.0f, -0.81f);
    pen.line(-1.0f, -0.27f, 1.0f, -0.27f);
    pen.line(-1.0f, 0.27f, 0.25f, 0.27f);
    pen.line(-1.0f, 0.81f, -0.12f, 0.81f);
}

void display(const Pen &pen)
{
    pen.polyline({-1.0f, -0.9f, 1.0f, -0.9f, 1.0f, 0.46f, -1.0f, 0.46f}, true);
    pen.line(0.0f, 0.46f, 0.0f, 0.9f);
    pen.line(-0.44f, 0.9f, 0.44f, 0.9f);
}

// An opening double quote above two lines of falling length. Each mark is a
// dot with a tail that rises from its left edge and hooks over to the right,
// the top of a "6"; a short sweep keeps it reading as a quote at 32 px.
void lyrics(const Pen &pen)
{
    constexpr float kDot = 0.21f;
    constexpr float kTail = 0.5f;
    constexpr float kMarkY = -0.32f;
    for (float mark_x : {-0.76f, -0.06f})
    {
        pen.dot(mark_x, kMarkY, kDot);
        pen.arc(mark_x - kDot + kTail, kMarkY, kTail, 0.75f * kTau, 0.22f * kTau);
    }
    pen.line(-0.97f, 0.36f, 0.97f, 0.36f);
    pen.line(-0.97f, 0.86f, 0.3f, 0.86f);
}

void check(const Pen &pen)
{
    pen.polyline({-0.86f, 0.06f, -0.3f, 0.6f, 0.88f, -0.58f});
}
} // namespace

void draw_icon(gfx::DrawList &list, Icon icon, float cx, float cy, float size, gfx::Color colour,
               float stroke)
{
    const Pen pen{list, cx, cy, std::max(size * 0.4f - stroke * 0.5f, 0.0f), stroke, colour};
    switch (icon)
    {
    case Icon::previous:
        previous(pen);
        break;
    case Icon::play:
        play(pen);
        break;
    case Icon::pause:
        pause(pen);
        break;
    case Icon::next:
        next(pen);
        break;
    case Icon::back10:
        chevrons(pen, 1.0f);
        break;
    case Icon::ahead10:
        chevrons(pen, -1.0f);
        break;
    case Icon::heart:
        heart_outline(pen, heart_shape());
        break;
    case Icon::heart_filled:
    {
        const HeartShape h = heart_shape();
        heart_fill(pen, h);
        heart_outline(pen, h);
        break;
    }
    case Icon::details:
        details(pen);
        break;
    case Icon::display:
        display(pen);
        break;
    case Icon::lyrics:
        lyrics(pen);
        break;
    case Icon::check:
        check(pen);
        break;
    case Icon::count:
        break;
    }
}
} // namespace hui::afterglow
