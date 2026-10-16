#!/usr/bin/env bash
# Afterglow - Build and exercise the native client against a local mock server.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$root/build/tests"
compile_flags=(-std=c++20 -Wall -Wextra -Wpedantic -Werror)
if [[ ${AFTERGLOW_TEST_DENY_FCNTL:-0} == 1 ]]; then
    compile_flags+=(-DAFTERGLOW_TEST_DENY_FCNTL=1 -Dfcntl=afterglow_denied_fcntl)
fi
"${CXX:-c++}" "${compile_flags[@]}" \
    -fno-exceptions -fno-rtti -pthread -I"$root/src" \
    "$root/src/afterglow/client.cpp" "$root/src/afterglow/protocol.cpp" \
    "$root/tests/integration/sendspin_client_smoke.cpp" \
    -o "$root/build/tests/sendspin_client_smoke"
python3 "$root/tests/integration/sendspin_mock_server.py" \
    "$root/build/tests/sendspin_client_smoke"
