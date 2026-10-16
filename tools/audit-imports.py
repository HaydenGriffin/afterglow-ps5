#!/usr/bin/env python3
# Afterglow - Lists the system imports of the PS5 executable and the module each binds to.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/audit-imports.py [build/llvm-pie.elf] [--baseline OTHER.elf]
#
# The converter (tooling/native/sce_module_writer.cpp) binds every undefined
# symbol to the first needed module, in link order, whose SDK stub exports
# it. libScePosixForWebKit is not loaded for a native title, so a symbol
# bound there is a null import at run time (arc4random_buf crashed the title
# that way). This repeats the converter's choice and fails when any symbol
# lands on that module. With --baseline it also lists the imports that are
# new against another build. Needs llvm-readelf and llvm-nm (the -18 names
# are tried first).

import argparse
import pathlib
import shutil
import subprocess
import sys

NULL_MODULES = {"libScePosixForWebKit.so"}


def tool(name):
    for candidate in (name + "-18", name):
        found = shutil.which(candidate)
        if found:
            return found
    sys.exit("missing " + name)


READELF, NM = tool("llvm-readelf"), tool("llvm-nm")


def needed(path):
    out = subprocess.check_output([READELF, "-d", str(path)], text=True)
    return [line.split("[", 1)[1].split("]", 1)[0] for line in out.splitlines() if "(NEEDED)" in line]


def soname(path):
    out = subprocess.check_output([READELF, "-d", str(path)], text=True)
    for line in out.splitlines():
        if "(SONAME)" in line:
            return line.split("[", 1)[1].split("]", 1)[0]
    return path.name


def symbols(path, flag):
    out = subprocess.check_output([NM, "-D", flag, str(path)], text=True)
    return {line.split()[-1] for line in out.splitlines() if line.strip()}


def bindings(elf, stub_dirs):
    stubs = {}
    for directory in stub_dirs:
        for stub in sorted(pathlib.Path(directory).glob("*.so")):
            stubs.setdefault(soname(stub), stub)
    order = needed(elf)
    exports = {name: symbols(stubs[name], "--defined-only") for name in order if name in stubs}
    result = {}
    for symbol in sorted(symbols(elf, "--undefined-only")):
        result[symbol] = next((name for name in order if symbol in exports.get(name, ())), None)
    return result


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", nargs="?", default=str(root / "build/llvm-pie.elf"))
    parser.add_argument("--baseline")
    args = parser.parse_args()
    stub_dirs = [root / ".deps/native/ps5-payload-sdk/target/lib",
                 root / ".deps/ps5-opengl/current/lib"]
    bound = bindings(args.elf, stub_dirs)
    by_module = {}
    for symbol, module in bound.items():
        by_module.setdefault(module or "(unresolved)", []).append(symbol)
    for module in sorted(by_module, key=str):
        print(f"{module}: {len(by_module[module])}")
    if args.baseline:
        before = bindings(args.baseline, stub_dirs)
        added = sorted(set(bound) - set(before))
        print(f"\nnew against {args.baseline}: {len(added)}")
        for symbol in added:
            print(f"  {symbol} -> {bound[symbol]}")
        for symbol in sorted(set(before) - set(bound)):
            print(f"  (gone) {symbol}")
    bad = sorted(s for s, m in bound.items() if m is None or m in NULL_MODULES)
    if bad:
        print("\nnull or unresolved imports:")
        for symbol in bad:
            print(f"  {symbol} -> {bound[symbol]}")
        return 1
    print("\nno import binds to " + ", ".join(sorted(NULL_MODULES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
