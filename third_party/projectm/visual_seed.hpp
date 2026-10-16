// Afterglow - Non-cryptographic seed source for projectM on PS5.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#ifdef __PROSPERO__
#include <atomic>
#include <chrono>
#include <cstdint>

// Only seeds visual effects. The console cannot resolve libc++ random_device's
// arc4random import; keep this replacement scoped to projectM, never security.
class AfterglowVisualSeed
{
  public:
    std::uint32_t operator()() const
    {
        static std::atomic<std::uint32_t> sequence{0};
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return static_cast<std::uint32_t>(now) ^
               (sequence.fetch_add(1, std::memory_order_relaxed) * 0x9e3779b9u);
    }
};
#else
#include <random>
using AfterglowVisualSeed = std::random_device;
#endif
