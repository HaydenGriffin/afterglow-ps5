// Afterglow - The night register: the poster steps down late in the evening.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace hui::afterglow
{
// LG's hours: from 22:00 until 06:00 local time.
constexpr bool night_hour(int hour)
{
    return hour >= 22 || (hour >= 0 && hour < 6);
}
// The local hour 0..23, or -1 when the clock cannot be read. The console asks
// the kernel to convert UTC to local time (its libc time zone data is not
// relied on); host builds use localtime_r.
int local_hour();
} // namespace hui::afterglow
