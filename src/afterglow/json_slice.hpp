// Afterglow - Slicing Music Assistant replies: a field, an array's elements, a string.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// The same bounded slicing companion.cpp does for its own replies, for the
// flows that read plain calls (playlists.hpp, programme.hpp): a value is
// found by walking the text, never by building a tree of a whole library.
namespace hui::afterglow::json_slice
{
// The raw value of a direct child of an object ("" when absent).
std::string_view field(std::string_view object, std::string_view key);
// The raw elements of an array, at most limit.
std::vector<std::string_view> elements(std::string_view array, std::size_t limit = 256);
// A string value decoded ("" for anything else).
std::string text(std::string_view object, std::string_view key);
// An integer value, fallback when absent or not a number.
long long integer(std::string_view object, std::string_view key, long long fallback = 0);
// A JSON string literal for a value.
std::string quote(std::string_view value);
} // namespace hui::afterglow::json_slice
