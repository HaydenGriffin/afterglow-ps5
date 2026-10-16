// Afterglow - Host check of the console keyboard backend against stubbed system calls.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// keyboard_ime.cpp only links on the console; here the sce functions it calls
// are stubs, so the module-load handling can be checked on a PC.

#include "platform/ps5/keyboard.hpp"
#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace
{
int load_result = 0, loads = 0, inits = 0;
}

namespace hui::sys
{
void log(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    std::fputc('\n', stderr);
}
} // namespace hui::sys

extern "C"
{
    int sceImeDialogInit(const void *, const void *)
    {
        ++inits;
        return 0;
    }
    int sceImeDialogGetStatus(void)
    {
        return 1;
    }
    int sceImeDialogGetResult(void *)
    {
        return 0;
    }
    int sceImeDialogAbort(void)
    {
        return 0;
    }
    int sceImeDialogTerm(void)
    {
        return 0;
    }
    int sceSysmoduleLoadModule(std::uint16_t)
    {
        ++loads;
        return load_result;
    }
}

int main()
{
    using hui::ps5::ImeBackend;
    auto backend = hui::ps5::make_ime_backend(1);
    char16_t buffer[8] = {};
    ImeBackend::Params params;
    params.title = u"Test";
    params.buffer = buffer;
    params.max_length = 7;
    // The module does not load: the open fails without touching the dialog.
    load_result = static_cast<int>(0x80020002u);
    const bool refused = !backend->open(params) && inits == 0;
    // It is tried again on the next open, and loaded only once.
    load_result = 0;
    const bool opened = backend->open(params) && inits == 1 && loads == 2;
    const bool once = backend->open(params) && inits == 2 && loads == 2;
    std::printf("refused=%d opened=%d once=%d\n", refused, opened, once);
    return refused && opened && once ? 0 : 1;
}
