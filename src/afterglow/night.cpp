// Afterglow - The night register: the poster steps down late in the evening.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/night.hpp"
#include <ctime>

#ifdef __PROSPERO__
// Not declared by the payload SDK. On the PS4 it is (time_t, time_t *, struct timesec { time_t;
// uint32 west_sec; uint32 dst_sec } *, unsigned long *): a 16-byte and an 8-byte write. Passing an
// int[2] and an int (as before) let those writes run over the result, and on a console every local
// time came out as midnight on 1 January 1970 (found by Lamplight for PS5 on system software 7.20,
// 2 Nov 2026). The buffers here are larger than any of them.
extern "C" int sceKernelConvertUtcToLocaltime(time_t utc, time_t *local, void *zone, void *dst);
#endif

namespace hui::afterglow
{
long long local_offset(long long utc_seconds)
{
#ifdef __PROSPERO__
    alignas(16) unsigned char zone[64] = {};
    alignas(16) unsigned char dst[64] = {};
    time_t local = 0;
    if (sceKernelConvertUtcToLocaltime(static_cast<time_t>(utc_seconds), &local, zone, dst) != 0)
        return 0;
    const long long offset = static_cast<long long>(local) - utc_seconds;
    // An offset no time zone has means the call didn't work: UTC rather than nonsense.
    return offset >= -14 * 3600 && offset <= 14 * 3600 && offset % 900 == 0 ? offset : 0;
#else
    const time_t at = static_cast<time_t>(utc_seconds);
    std::tm parts{};
    return localtime_r(&at, &parts) != nullptr ? parts.tm_gmtoff : 0;
#endif
}
int local_hour()
{
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return -1;
    const long long local = static_cast<long long>(now.tv_sec) + local_offset(now.tv_sec);
    return static_cast<int>(((local % 86400) + 86400) % 86400 / 3600);
}
} // namespace hui::afterglow
