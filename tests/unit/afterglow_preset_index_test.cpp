// Afterglow - The packaged MilkDrop presets match the pinned selection LG Afterglow ships.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

#ifndef HUI_SOURCE_DIR
#define HUI_SOURCE_DIR "."
#endif

namespace
{
const std::string kRoot = HUI_SOURCE_DIR;

// "first<TAB>second" lines; '#' lines are comments.
std::vector<std::pair<std::string, std::string>> read_pairs(const std::string &path)
{
    std::vector<std::pair<std::string, std::string>> pairs;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        const auto tab = line.find('\t');
        pairs.emplace_back(line.substr(0, tab),
                           tab == std::string::npos ? "" : line.substr(tab + 1));
    }
    return pairs;
}

std::string preset_file(std::size_t number)
{
    char name[16];
    std::snprintf(name, sizeof(name), "%04zu.milk", number);
    return name;
}

bool exists(const std::string &path)
{
    std::ifstream in(path);
    return in.good();
}
} // namespace

TEST(AfterglowPresetIndex, PackagesAllOfLgsPresetsInListOrder)
{
    const auto selection = read_pairs(kRoot + "/tools/butterchurn-presets.tsv");
    ASSERT_EQ(selection.size(), 395U);
    for (std::size_t i = 0; i < selection.size(); ++i)
    {
        EXPECT_EQ(selection[i].first.size(), 64U) << selection[i].second;
        EXPECT_TRUE(exists(kRoot + "/assets/presets/" + preset_file(i + 1))) << preset_file(i + 1);
    }
    EXPECT_FALSE(exists(kRoot + "/assets/presets/" + preset_file(selection.size() + 1)));
}

TEST(AfterglowPresetIndex, OffersEveryPresetButTheTwoColourRampOnes)
{
    const auto selection = read_pairs(kRoot + "/tools/butterchurn-presets.tsv");
    const auto index = read_pairs(kRoot + "/assets/presets/index.tsv");
    ASSERT_EQ(index.size(), 393U);
    std::set<std::string> offered;
    for (const auto &[file, name] : index)
    {
        EXPECT_TRUE(offered.insert(name).second) << "listed twice: " << name;
        // Each index line names the file the selection gives that preset.
        std::size_t number = 0;
        while (number < selection.size() && selection[number].second != name)
            ++number;
        ASSERT_LT(number, selection.size()) << "not in the selection: " << name;
        EXPECT_EQ(file, preset_file(number + 1)) << name;
    }
    std::set<std::string> left_out;
    for (const auto &entry : selection)
        if (offered.count(entry.second) == 0)
            left_out.insert(entry.second);
    EXPECT_EQ(left_out,
              (std::set<std::string>{"ORB - Planetary Alignment Acid Burn", "orb - toxic goo"}));
}
