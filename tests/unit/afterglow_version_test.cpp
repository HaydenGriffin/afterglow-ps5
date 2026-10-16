// Afterglow - The app version and param.json's contentVersion agree.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/version.hpp"
#include "core/version.hpp"
#include <cstdio>
#include <gtest/gtest.h>

#ifndef HUI_SOURCE_DIR
#define HUI_SOURCE_DIR "."
#endif

TEST(AfterglowVersion, MatchesParamJsonContentVersion)
{
    unsigned major = 0, minor = 0, patch = 0;
    ASSERT_EQ(std::sscanf(hui::afterglow::kVersion, "%u.%u.%u", &major, &minor, &patch), 3);
    char expected[16];
    std::snprintf(expected, sizeof(expected), "%02u.%03u.%03u", major, minor, patch);
    EXPECT_EQ(hui::read_content_version(std::string(HUI_SOURCE_DIR) + "/sce_sys/param.json"),
              expected);
}
