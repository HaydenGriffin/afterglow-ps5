#!/usr/bin/env bash
# Afterglow - Library paging, queue window and grouping against a disposable server.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build/tests"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
    -fno-exceptions -fno-rtti -pthread -I"$root/src" \
    "$root/src/afterglow/companion.cpp" "$root/src/afterglow/speakers.cpp" "$root/src/afterglow/controls.cpp" \
    "$root/src/afterglow/share_pick.cpp" \
    "$root/src/afterglow/speaker_group.cpp" \
    "$root/src/afterglow/ma_socket.cpp" \
    "$root/src/afterglow/protocol.cpp" \
    "$root/tests/integration/companion_library_smoke.cpp" \
    -o "$root/build/tests/companion_library_smoke"
python3 "$root/tests/integration/companion_library_mock.py" \
    "$root/build/tests/companion_library_smoke"
