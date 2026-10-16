#!/usr/bin/env bash
# Afterglow - Build the Music Assistant worker against a disposable local server.
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
    "$root/tests/integration/companion_client_smoke.cpp" \
    -o "$root/build/tests/companion_client_smoke"
python3 "$root/tests/integration/companion_mock_server.py" \
    "$root/build/tests/companion_client_smoke"
# Following with the relay on: a paused speaker stays the target (no relay opens).
python3 "$root/tests/integration/companion_mock_server.py" \
    "$root/build/tests/companion_client_smoke" follow
# Presses combined behind slow answers, the renewed token taking over from a
# refused one, and the relay's stall reconnect (about 45 s in all).
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
    -fno-exceptions -fno-rtti -pthread -I"$root/src" \
    "$root/src/afterglow/companion.cpp" "$root/src/afterglow/speakers.cpp" "$root/src/afterglow/controls.cpp" \
    "$root/src/afterglow/share_pick.cpp" \
    "$root/src/afterglow/speaker_group.cpp" \
    "$root/src/afterglow/ma_socket.cpp" \
    "$root/src/afterglow/protocol.cpp" \
    "$root/tests/integration/companion_controls_smoke.cpp" \
    -o "$root/build/tests/companion_controls_smoke"
python3 "$root/tests/integration/companion_controls_mock.py" \
    "$root/build/tests/companion_controls_smoke" controls
python3 "$root/tests/integration/companion_controls_mock.py" \
    "$root/build/tests/companion_controls_smoke" relay
