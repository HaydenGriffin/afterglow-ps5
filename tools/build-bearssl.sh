#!/usr/bin/env bash
# Afterglow - Builds BearSSL (TLS for the online lyrics search) for the PS5 and the host.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/build-bearssl.sh [ps5|host|all]
#
# Fetches BearSSL at a pinned commit (the commit id is the checksum of the
# whole tree) into .deps/bearssl/src and compiles the library into
#   .deps/bearssl/ps5/libbearssl.a    PS5 target, tooling/prospero-clang18
#   .deps/bearssl/host/libbearssl.a   Linux host, clang-18 (Mesa preview)
# with the public headers in .deps/bearssl/src/inc.
#
# BearSSL needs no allocator, no threads and no system calls of its own. On
# the console its OS hooks are switched off: it gets its randomness from the
# CPU (RDRAND) and the time from the caller, so it adds no imports that only
# libScePosixForWebKit would provide (those are null in a native title).

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
targets=${1:-all}
case $targets in
    ps5|host) ;;
    all) targets="ps5 host" ;;
    *) echo "usage: tools/build-bearssl.sh [ps5|host|all]" >&2; exit 2 ;;
esac

commit=7bea48e5e850ab4cafbe68d3765cdaba13a86d6f
url=https://www.bearssl.org/git/BearSSL

base="$root/.deps/bearssl"
src="$base/src"

fetch_source() {
    if [[ -d $src/.git ]] && [[ $(git -C "$src" rev-parse HEAD 2>/dev/null) == "$commit" ]] &&
        [[ -f $src/inc/bearssl.h ]]; then
        return
    fi
    printf '==> [bearssl] Fetching BearSSL %s\n' "${commit:0:12}"
    rm -rf -- "$src"
    # The upstream server is not asked for a single commit; the history is small.
    git clone --quiet "$url" "$src"
    git -C "$src" checkout --quiet --detach "$commit"
    [[ $(git -C "$src" rev-parse HEAD) == "$commit" ]] || {
        echo "BearSSL checkout is not $commit" >&2
        exit 1
    }
}

build_target() {
    local target=$1 out="$base/$1" ar file object relative
    local -a cc_command objects config
    ninja_begin "$out/build.ninja"
    if [[ $target == ps5 ]]; then
        local sdk="$root/.deps/native/ps5-payload-sdk"
        local compiler
        compiler=$(command -v "${PS5_CLANG:-clang-18}")
        [[ -d $sdk ]] || { echo "PS5 payload SDK missing: run make deps first" >&2; exit 2; }
        cc_command=(env PS5_PAYLOAD_SDK="$sdk" PS5_CLANG="$compiler"
            USE_CCACHE="${USE_CCACHE:-1}" sh "$root/tooling/prospero-clang18")
        ar="$sdk/bin/llvm-ar"
        config=(-DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_WIN32_RAND=0
            -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 -DBR_RDRAND=1)
    else
        cc_command=("${compiler_cache[@]}" "$(command -v "${HOST_CC:-clang-18}")" -fPIC)
        ar=$(command -v llvm-ar-18 || command -v ar)
        config=()
    fi
    objects=()
    while IFS= read -r -d '' file; do
        relative=${file#"$src/src/"}
        object="$out/obj/${relative//\//_}.o"
        ninja_inputs=("$file")
        ninja_edge CC "$object" "${cc_command[@]}" -std=gnu99 -O2 -w -ffunction-sections \
            -fdata-sections "${config[@]}" -I"$src/inc" -I"$src/src" \
            -MD -MF "$object.d" -c "$file" -o "$object"
        objects+=("$object")
    done < <(find "$src/src" -name '*.c' -print0 | sort -z)
    ninja_inputs=("${objects[@]}")
    ninja_edge AR "$out/libbearssl.a" bash -c 'rm -f "$0" && "$1" rcs "$0" "${@:2}"' \
        "$out/libbearssl.a" "$ar" "${objects[@]}"
    printf '==> [bearssl] Building %s\n' "$target"
    if ! ninja_run >"$out/build.log" 2>&1; then
        grep -E 'error|FAILED' -A6 "$out/build.log" | cut -c1-300 | head -80 >&2
        exit 1
    fi
    printf '==> [bearssl] %s\n' "${out#"$root/"}/libbearssl.a"
}

source "$root/tools/ninja-build.sh"
fetch_source
for target in $targets; do
    build_target "$target"
done
