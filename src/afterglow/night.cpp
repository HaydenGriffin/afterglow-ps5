// Afterglow - The night register: the poster steps down late in the evening.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "afterglow/night.hpp"
#include <ctime>

#ifdef __PROSPERO__
// zone is a BSD struct timezone (two ints); real buffers are passed rather
// than null, as the kernel may write them unconditionally.
extern "C" int sceKernelConvertUtcToLocaltime(time_t utc, time_t *local, void *zone,
                                              int *dst_seconds);
#endif

namespace hui::afterglow
{
int local_hour()
{
    timespec now{};
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return -1;
#ifdef __PROSPERO__
    // The kernel applies the console's time zone setting (the system's own
    // libc imports the same call for localtime). Whether the result already
    // includes summer time is not yet confirmed on hardware.
    time_t local = 0;
    int zone[2] = {}, dst = 0;
    if (sceKernelConvertUtcToLocaltime(now.tv_sec, &local, zone, &dst) != 0)
        return -1;
    const long long seconds = static_cast<long long>(local) % 86400;
    return static_cast<int>((seconds + 86400) % 86400 / 3600);
#else
    std::tm parts{};
    if (localtime_r(&now.tv_sec, &parts) == nullptr)
        return -1;
    return parts.tm_hour;
#endif
}
} // namespace hui::afterglow
