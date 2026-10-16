/* Afterglow - libc gaps that libprojectM's libc++ dependencies open on the PS5.
 * Copyright (C) 2026 Afterglow contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libprojectM's texture scanner uses std::filesystem, and libc++ keeps
 * last_write_time() in the same object, so the link needs utimensat. The
 * payload SDK's libc.a has it only inside syscalls.o, which would also replace
 * hundreds of libkernel imports with raw system calls. projectM never sets a
 * file's times, so a refusal is enough. Compiled into the PS5 archive only.
 */

#include <errno.h>
#include <sys/stat.h>

int utimensat(int fd, const char *path, const struct timespec times[2], int flag)
{
    (void)fd;
    (void)path;
    (void)times;
    (void)flag;
    errno = ENOSYS;
    return -1;
}
