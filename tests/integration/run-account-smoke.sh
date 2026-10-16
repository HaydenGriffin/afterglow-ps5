#!/usr/bin/env bash
# Afterglow - Build the sign-in worker and account mirroring against disposable local servers.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build/tests"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
    -fno-exceptions -fno-rtti -pthread -I"$root/src" \
    "$root/src/afterglow/account.cpp" "$root/src/afterglow/ma_socket.cpp" \
    "$root/src/afterglow/preferences.cpp" "$root/src/afterglow/protocol.cpp" \
    "$root/src/core/save_file.cpp" \
    "$root/tests/integration/account_client_smoke.cpp" \
    -o "$root/build/tests/account_client_smoke"
python3 "$root/tests/integration/account_mock_server.py" \
    "$root/build/tests/account_client_smoke"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Wpedantic -Werror \
    -fno-exceptions -fno-rtti -pthread -I"$root/src" \
    "$root/src/afterglow/companion.cpp" "$root/src/afterglow/speakers.cpp" "$root/src/afterglow/ma_socket.cpp" \
    "$root/src/afterglow/protocol.cpp" \
    "$root/tests/integration/companion_account_smoke.cpp" \
    -o "$root/build/tests/companion_account_smoke"
python3 "$root/tests/integration/companion_account_mock_server.py" \
    "$root/build/tests/companion_account_smoke"
