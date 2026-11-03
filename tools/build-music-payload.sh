#!/usr/bin/env bash
# Afterglow - Builds the Music over games payload, which runs beside the console's music player.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A plain ELF for the console's loader, separate from the app: it shares the Sendspin client with
# the app and holds more audio, because the USB stick it writes to can stall for seconds.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk="$root/.deps/native/ps5-payload-sdk"
out="${1:-$root/build/payload/afterglow-music.elf}"
[[ -x $sdk/bin/prospero-clang++ ]] || {
    echo "the PS5 payload SDK is missing: run make deps first" >&2
    exit 2
}
mkdir -p "$(dirname -- "$out")"
"$sdk/bin/prospero-clang++" -std=c++20 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti \
    -I"$root/src" -I"$root/payload" \
    -DAFTERGLOW_PACKET_COUNT=512 -DAFTERGLOW_BUFFER_BYTES=768000 -DAFTERGLOW_QUEUE_BYTES=1728000 \
    "$root/payload/main.cpp" "$root/src/afterglow/client.cpp" "$root/src/afterglow/protocol.cpp" \
    "$root/src/afterglow/controls.cpp" -lkernel_sys -lSceNotification -o "$out"
printf 'Payload: %s\n' "$out"
