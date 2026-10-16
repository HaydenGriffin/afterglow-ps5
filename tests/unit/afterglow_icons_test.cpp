// Afterglow - Stroked line icon geometry tests.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/icons.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using hui::afterglow::draw_icon;
using hui::afterglow::Icon;
using hui::gfx::Color;
using hui::gfx::DrawList;

namespace
{
constexpr float kCx = 400.0f;
constexpr float kCy = 300.0f;
constexpr Color kInk{0.9f, 0.4f, 0.2f, 1.0f};

struct Box
{
    float x0 = 1e9f;
    float y0 = 1e9f;
    float x1 = -1e9f;
    float y1 = -1e9f;

    void add(float x, float y)
    {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
};

// Instance rectangles are the quads the shader covers (a whole circle for an
// arc), so containing them is stricter than containing the visible ink.
Box extent(const DrawList &list)
{
    Box box;
    for (const auto &i : list.instances())
    {
        box.add(i.rect[0], i.rect[1]);
        box.add(i.rect[0] + i.rect[2], i.rect[1] + i.rect[3]);
    }
    for (const auto &v : list.mesh_vertices())
        box.add(v.x, v.y);
    return box;
}

std::string name_of(int index)
{
    return "icon " + std::to_string(index);
}

constexpr int kIcons = static_cast<int>(Icon::count);
} // namespace

TEST(AfterglowIcons, EveryIconDrawsSomething)
{
    for (int i = 0; i < kIcons; ++i)
    {
        DrawList list;
        draw_icon(list, static_cast<Icon>(i), kCx, kCy, 48.0f, kInk);
        EXPECT_FALSE(list.empty()) << name_of(i);
    }
}

TEST(AfterglowIcons, StaysInsideTheSquareAtEveryStrokeWeight)
{
    const float sizes[] = {24.0f, 48.0f, 96.0f};
    const float strokes[] = {1.5f, 3.0f, 5.0f};
    for (float size : sizes)
        for (float stroke : strokes)
            for (int i = 0; i < kIcons; ++i)
            {
                DrawList list;
                draw_icon(list, static_cast<Icon>(i), kCx, kCy, size, kInk, stroke);
                const Box box = extent(list);
                const float half = size * 0.5f + 0.01f;
                EXPECT_GE(box.x0, kCx - half) << name_of(i) << " size " << size;
                EXPECT_GE(box.y0, kCy - half) << name_of(i) << " size " << size;
                EXPECT_LE(box.x1, kCx + half) << name_of(i) << " size " << size;
                EXPECT_LE(box.y1, kCy + half) << name_of(i) << " size " << size;
            }
}

TEST(AfterglowIcons, FillsMostOfTheSquareSoIconsShareAnOpticalSize)
{
    for (int i = 0; i < kIcons; ++i)
    {
        DrawList list;
        draw_icon(list, static_cast<Icon>(i), kCx, kCy, 48.0f, kInk);
        const Box box = extent(list);
        EXPECT_GE(std::max(box.x1 - box.x0, box.y1 - box.y0), 48.0f * 0.65f) << name_of(i);
    }
}

TEST(AfterglowIcons, GeometryScalesWithSize)
{
    // With no stroke the outline is pure geometry, so doubling the size
    // doubles the extent about the centre.
    for (int i = 0; i < kIcons; ++i)
    {
        DrawList small;
        DrawList large;
        draw_icon(small, static_cast<Icon>(i), kCx, kCy, 40.0f, kInk, 0.0f);
        draw_icon(large, static_cast<Icon>(i), kCx, kCy, 80.0f, kInk, 0.0f);
        const Box a = extent(small);
        const Box b = extent(large);
        EXPECT_NEAR(b.x0 - kCx, 2.0f * (a.x0 - kCx), 0.01f) << name_of(i);
        EXPECT_NEAR(b.y1 - kCy, 2.0f * (a.y1 - kCy), 0.01f) << name_of(i);
    }
}

TEST(AfterglowIcons, UsesTheGivenColour)
{
    for (int i = 0; i < kIcons; ++i)
    {
        DrawList list;
        draw_icon(list, static_cast<Icon>(i), kCx, kCy, 48.0f, kInk);
        for (const auto &inst : list.instances())
        {
            EXPECT_FLOAT_EQ(inst.color_top[0], kInk.r) << name_of(i);
            EXPECT_FLOAT_EQ(inst.color_top[1], kInk.g) << name_of(i);
            EXPECT_FLOAT_EQ(inst.color_top[2], kInk.b) << name_of(i);
            EXPECT_FLOAT_EQ(inst.color_top[3], kInk.a) << name_of(i);
            EXPECT_FLOAT_EQ(inst.color_bottom[0], kInk.r) << name_of(i);
        }
        for (const auto &v : list.mesh_vertices())
        {
            EXPECT_FLOAT_EQ(v.r, kInk.r) << name_of(i);
            EXPECT_FLOAT_EQ(v.g, kInk.g) << name_of(i);
            EXPECT_FLOAT_EQ(v.b, kInk.b) << name_of(i);
            EXPECT_FLOAT_EQ(v.a, kInk.a) << name_of(i);
        }
    }
}

TEST(AfterglowIcons, OnlyTheFilledHeartHasAMesh)
{
    for (int i = 0; i < kIcons; ++i)
    {
        DrawList list;
        draw_icon(list, static_cast<Icon>(i), kCx, kCy, 48.0f, kInk);
        EXPECT_EQ(!list.mesh_vertices().empty(), static_cast<Icon>(i) == Icon::heart_filled)
            << name_of(i);
    }
}

TEST(AfterglowIcons, CountDrawsNothing)
{
    DrawList list;
    draw_icon(list, Icon::count, kCx, kCy, 48.0f, kInk);
    EXPECT_TRUE(list.empty());
}
