#!/usr/bin/env bash
# Afterglow - Builds libprojectM (MilkDrop visualiser) static archives for the PS5 and the host.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/build-projectm.sh [ps5|host|all]
#
# Fetches projectM at a pinned commit into .deps/projectm/src, generates the
# headers its CMake build would configure, and compiles the C API core (no
# playlist library, no SDL test UI) into
#   .deps/projectm/ps5/libprojectM.a    PS5 target, tooling/prospero-clang18
#   .deps/projectm/host/libprojectM.a   Linux host, clang-18 (Mesa preview, tests)
# with the public headers in .deps/projectm/include/projectM-4.
#
# projectM reports bad presets and shaders by throwing, so it is compiled with
# -fexceptions; the app's link already carries libunwind and libc++abi, and the
# app talks to projectM through the C API; the engine catches
# preset-load failures internally. Allocation failures remain fatal.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
targets=${1:-all}
case $targets in
    ps5|host) ;;
    all) targets="ps5 host" ;;
    *) echo "usage: tools/build-projectm.sh [ps5|host|all]" >&2; exit 2 ;;
esac

commit=349b7cc2db312d47cef6be24c6856ced6643a385
version=4.2.0
url=https://github.com/projectM-visualizer/projectm.git

base="$root/.deps/projectm"
src="$base/src"
gen="$base/gen"
include="$base/include"

fetch_source() {
    if [[ -d $src/.git ]] && [[ $(git -C "$src" rev-parse HEAD 2>/dev/null) == "$commit" ]] &&
        [[ -f $src/vendor/projectm-eval/projectm-eval/api/projectm-eval.h ]]; then
        return
    fi
    printf '==> [projectm] Fetching projectM %s\n' "${commit:0:12}"
    rm -rf -- "$src"
    mkdir -p "$src"
    git -C "$src" init --quiet
    git -C "$src" remote add origin "$url"
    git -C "$src" fetch --quiet --depth 1 origin "$commit"
    git -C "$src" checkout --quiet FETCH_HEAD
    # The superproject pins projectm-eval; only that submodule is needed.
    # Full submodule history avoids missing-tree failures when the pinned commit
    # is older than the shallow remote boundary.
    git -C "$src" submodule --quiet update --init vendor/projectm-eval
}

# Writes stdin to a file only when it changed, so Ninja does not rebuild.
write_if_changed() {
    local file=$1 temporary
    temporary=$(mktemp)
    cat >"$temporary"
    if cmp -s "$temporary" "$file"; then
        rm -f -- "$temporary"
    else
        mkdir -p "$(dirname "$file")"
        mv -- "$temporary" "$file"
        chmod 644 "$file"
    fi
}

# Stand-ins for the files projectM's CMake build configures.
generate_headers() {
    write_if_changed "$gen/config.h" <<'EOF'
#pragma once
#define PROJECTM_USE_THREADS 0
#define VERSION "4.2.0"
#define PROJECTM_FILESYSTEM_NAMESPACE std
#define PROJECTM_FILESYSTEM_INCLUDE <filesystem>
EOF
    write_if_changed "$gen/projectM-4/version.h" <<EOF
#pragma once
#define PROJECTM_VERSION_MAJOR ${version%%.*}
#define PROJECTM_VERSION_MINOR $(cut -d. -f2 <<<"$version")
#define PROJECTM_VERSION_PATCH ${version##*.}
#define PROJECTM_VERSION_STRING "$version"
#define PROJECTM_VERSION_VCS "${commit:0:12}"
EOF
    # Static archives only: every export macro is empty.
    local name upper
    for name in projectM projectM_cxx; do
        upper=${name^^}
        write_if_changed "$gen/projectM-4/${name}_export.h" <<EOF
#pragma once
#define ${upper}_EXPORT
#define ${upper}_NO_EXPORT
#define ${upper}_DEPRECATED
#define ${upper}_DEPRECATED_EXPORT
#define ${upper}_DEPRECATED_NO_EXPORT
EOF
    done

    # The shader sources embedded as C++ strings, as the CMake scripts
    # GenerateStaticShaders.cmake and GenerateShaderResourcesScript.cmake do.
    python3 - "$src/src/libprojectM" "$gen" <<'PY'
import os, re, sys
from pathlib import Path

library, gen = Path(sys.argv[1]), Path(sys.argv[2])

def write(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)

def contents(files):
    return "".join('static std::string k%s = R"(\n%s)";\n\n' % (f.stem, f.read_text())
                   for f in files)

milkdrop = library / "MilkdropPreset"
shaders = sorted((milkdrop / "Shaders").iterdir())
declarations = definitions = ""
for shader in shaders:
    accessor = re.sub(r"Glsl([0-9]+)", "", shader.stem)
    declarations += "    std::string Get%s();\n" % accessor
    macro = "DECLARE_SHADER_ACCESSOR_NO_HEADER" if shader.suffix == ".inc" else "DECLARE_SHADER_ACCESSOR"
    definitions += "%s(%s);\n" % (macro, accessor)
for extension in ("hpp", "cpp"):
    text = (milkdrop / ("MilkdropStaticShaders.%s.in" % extension)).read_text()
    text = (text.replace("@STATIC_SHADER_CONTENTS@", contents(shaders))
            .replace("@STATIC_SHADER_ACCESSOR_DECLARATIONS@", declarations)
            .replace("@STATIC_SHADER_ACCESSOR_DEFINITIONS@", definitions))
    write(gen / ("MilkdropStaticShaders." + extension), text)

for name, folder in (("SpriteShaders.hpp", "UserSprites/Shaders"),
                     ("BuiltInTransitionsResources.hpp", "Renderer/TransitionShaders")):
    files = sorted((library / folder).iterdir())
    write(gen / name, "#pragma once\n\n#include <string>\n\n" + contents(files))
PY
}

install_headers() {
    mkdir -p "$include/projectM-4"
    local header
    for header in "$src"/src/api/include/projectM-4/*.h "$gen/projectM-4/version.h" \
        "$gen/projectM-4/projectM_export.h"; do
        write_if_changed "$include/projectM-4/${header##*/}" <"$header"
    done
}

collect_sources() {
    local library="$src/src/libprojectM"
    cpp_sources=()
    while IFS= read -r -d '' file; do
        case ${file#"$library/"} in
            Renderer/Platform/DynamicLibrary_*.cpp|StaticLibDummy.cpp) continue ;;
        esac
        cpp_sources+=("$file")
    done < <(find "$library" -name '*.cpp' -print0 | sort -z)
    cpp_sources+=("$gen/MilkdropStaticShaders.cpp")
    cpp_sources+=("$src"/vendor/hlslparser/src/*.cpp)

    local eval="$src/vendor/projectm-eval/projectm-eval"
    c_sources=("$eval"/*.c "$eval/api/projectm-eval.c" "$src/vendor/glad/src/gl.c"
        "$src/vendor/stb_image/stb_image.c" "$src/vendor/stb_image/image_DXT.c"
        "$src/vendor/stb_image/wfETC.c")
}

build_target() {
    local target=$1 out="$base/$1" cc cxx ar file object relative
    local -a cc_command cxx_command common objects egl_include
    ninja_begin "$out/build.ninja"
    common=(-O2 -w -ffunction-sections -fdata-sections
        -DPROJECTM_STATIC_DEFINE -DPROJECTM_CXX_STATIC_DEFINE -DPRJM_F_SIZE=8
        -include "$gen/config.h" -I"$gen" -I"$gen/projectM-4" -I"$src/src"
        -I"$src/src/libprojectM" -I"$src/src/libprojectM/Renderer"
        -I"$src/src/libprojectM/MilkdropPreset" -I"$src/src/api/include"
        -I"$src/vendor" -I"$src/vendor/glad/include" -I"$src/vendor/hlslparser/src"
        -I"$src/vendor/stb_image" -I"$src/vendor/projectm-eval"
        -I"$src/vendor/projectm-eval/projectm-eval/api")
    if [[ $target == ps5 ]]; then
        local sdk="$root/.deps/native/ps5-payload-sdk"
        local compiler
        compiler=$(command -v "${PS5_CLANG:-clang-18}")
        [[ -d $sdk ]] || { echo "PS5 payload SDK missing: run make deps first" >&2; exit 2; }
        cc_command=(env PS5_PAYLOAD_SDK="$sdk" PS5_CLANG="$compiler"
            USE_CCACHE="${USE_CCACHE:-1}" sh "$root/tooling/prospero-clang18")
        cxx_command=("${cc_command[@]}")
        ar="$sdk/bin/llvm-ar"
        egl_include=(-I"$root/.deps/ps5-opengl/current/include")
    else
        cc=$(command -v "${HOST_CC:-clang-18}")
        cxx=$(command -v "${HOST_CXX:-clang++-18}")
        cc_command=("${compiler_cache[@]}" "$cc" -fPIC)
        cxx_command=("${compiler_cache[@]}" "$cxx" -fPIC)
        ar=$(command -v llvm-ar-18 || command -v ar)
        egl_include=()
    fi

    objects=()
    for file in "${c_sources[@]}" "${cpp_sources[@]}"; do
        relative=${file#"$base/"}
        object="$out/obj/${relative//\//_}.o"
        ninja_inputs=("$file")
        if [[ $file == *.c ]]; then
            ninja_edge CC "$object" "${cc_command[@]}" -std=gnu11 "${common[@]}" \
                -MD -MF "$object.d" -c "$file" -o "$object"
        else
            ninja_edge CXX "$object" "${cxx_command[@]}" -std=c++17 -fexceptions "${common[@]}" \
                -include "$root/third_party/projectm/visual_seed.hpp" \
                -MD -MF "$object.d" -c "$file" -o "$object"
        fi
        objects+=("$object")
    done
    # Our own files build with warnings on; upstream's are compiled as published.
    local own=(-O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections)
    object="$out/obj/DynamicLibrary_static.cpp.o"
    ninja_inputs=("$root/third_party/projectm/DynamicLibrary_static.cpp")
    ninja_edge CXX "$object" "${cxx_command[@]}" -std=c++17 -fexceptions "${own[@]}" \
        "${egl_include[@]}" -I"$src/src/libprojectM/Renderer" \
        -MD -MF "$object.d" -c "$root/third_party/projectm/DynamicLibrary_static.cpp" -o "$object"
    objects+=("$object")
    if [[ $target == ps5 ]]; then
        object="$out/obj/ps5_compat.c.o"
        ninja_inputs=("$root/third_party/projectm/ps5_compat.c")
        ninja_edge CC "$object" "${cc_command[@]}" -std=c11 "${own[@]}" \
            -MD -MF "$object.d" -c "$root/third_party/projectm/ps5_compat.c" -o "$object"
        objects+=("$object")
        # libc++'s iostreams (projectM parses presets with string streams) call
        # the xlocale *_l functions, which the system libc does not export.
        # The SDK's libc.a implements them for the C locale in these members;
        # taking only them keeps the rest of libc.a (syslog, raw system calls)
        # from replacing imports and runtime shims.
        mkdir -p "$out/libc"
        (cd "$out/libc" && "$ar" x "$sdk/target/lib/libc.a" no-locale.o mbsnrtowcs.o wcsnrtombs.o)
        objects+=("$out/libc/no-locale.o" "$out/libc/mbsnrtowcs.o" "$out/libc/wcsnrtombs.o")
    fi
    ninja_inputs=("${objects[@]}")
    ninja_edge AR "$out/libprojectM.a" bash -c 'rm -f "$0" && "$1" rcs "$0" "${@:2}"' \
        "$out/libprojectM.a" "$ar" "${objects[@]}"
    printf '==> [projectm] Building %s\n' "$target"
    if ! ninja_run >"$out/build.log" 2>&1; then
        grep -E 'error|FAILED' -A6 "$out/build.log" | cut -c1-300 | head -80 >&2
        exit 1
    fi
    printf '==> [projectm] %s\n' "${out#"$root/"}/libprojectM.a"
}

source "$root/tools/ninja-build.sh"
fetch_source
for patch in "$root"/third_party/projectm/*.patch; do
    if ! git -C "$src" apply --reverse --check "$patch" 2>/dev/null; then
        git -C "$src" apply --check "$patch"
        git -C "$src" apply "$patch"
    fi
done
generate_headers
install_headers
collect_sources
for target in $targets; do
    build_target "$target"
done
