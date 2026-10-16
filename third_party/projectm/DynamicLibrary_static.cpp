// Afterglow - libprojectM's DynamicLibrary backend for an EGL linked into the executable.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replaces upstream's dlopen backends (Renderer/Platform/DynamicLibrary_*.cpp).
// The PS5 has no system libEGL to open: ps5-opengl is linked statically, so
// nothing is ever loaded. projectM's GL resolver still needs a few EGL entry
// points to see the app's current context (its strict context gate refuses to
// start without eglGetCurrentContext), so "global symbols" are a fixed table
// of the linked EGL functions. Every GL function comes from the app's
// load_proc (eglGetProcAddress) passed to projectm_create_with_opengl_load_proc.

#include "Platform/DynamicLibrary.hpp"

#include <EGL/egl.h>

#include <cstring>
#include <string>

namespace libprojectM
{
namespace Renderer
{
namespace Platform
{

namespace
{

struct LinkedSymbol
{
    const char *name;
    void *address;
};

auto linked_symbols() -> const LinkedSymbol (&)[5]
{
    static const LinkedSymbol symbols[] = {
        {"eglGetProcAddress", FunctionToSymbol(&eglGetProcAddress)},
        {"eglGetCurrentContext", FunctionToSymbol(&eglGetCurrentContext)},
        {"eglGetCurrentDisplay", FunctionToSymbol(&eglGetCurrentDisplay)},
        {"eglQueryString", FunctionToSymbol(&eglQueryString)},
        {"eglGetError", FunctionToSymbol(&eglGetError)},
    };
    return symbols;
}

} // namespace

auto DynamicLibrary::Open(const char *const * /*names*/, std::string &reason) -> bool
{
    reason = "dynamic loading is not used: EGL and GL are linked statically";
    return false;
}

void DynamicLibrary::Close()
{
}

auto DynamicLibrary::GetSymbol(const char * /*name*/) const -> void *
{
    return nullptr;
}

auto DynamicLibrary::FindGlobalSymbol(const char *name) -> void *
{
    if (name == nullptr)
    {
        return nullptr;
    }
    for (const LinkedSymbol &symbol : linked_symbols())
    {
        if (std::strcmp(symbol.name, name) == 0)
        {
            return symbol.address;
        }
    }
    return nullptr;
}

} // namespace Platform
} // namespace Renderer
} // namespace libprojectM
