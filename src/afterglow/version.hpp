// Afterglow - The one app version string.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace hui::afterglow
{
// The release version. sce_sys/param.json's contentVersion carries the same
// number as NN.NNN.NNN (1.6.0 is "01.006.000"); a unit test keeps them equal.
inline constexpr const char *kVersion = "1.7.0";
} // namespace hui::afterglow
