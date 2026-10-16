#!/usr/bin/env bash
# Afterglow - Build the Home Assistant glance client against a disposable local server.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build/tests"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
    -fno-exceptions -fno-rtti -pthread -I"$root/src" \
    "$root/src/afterglow/home_assistant.cpp" "$root/src/afterglow/protocol.cpp" \
    "$root/tests/integration/home_assistant_smoke.cpp" \
    -o "$root/build/tests/home_assistant_smoke"
python3 "$root/tests/integration/home_assistant_mock_server.py" \
    "$root/build/tests/home_assistant_smoke"
