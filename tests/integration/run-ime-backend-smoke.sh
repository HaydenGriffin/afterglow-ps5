#!/usr/bin/env bash
# Afterglow - Build the console keyboard backend against stubbed system calls.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build/tests"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
    -fno-exceptions -fno-rtti -I"$root/src" \
    "$root/src/platform/ps5/keyboard_ime.cpp" \
    "$root/tests/integration/ime_backend_smoke.cpp" \
    -o "$root/build/tests/ime_backend_smoke"
"$root/build/tests/ime_backend_smoke"
