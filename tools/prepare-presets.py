#!/usr/bin/env python3
# Afterglow - Package MilkDrop .milk presets for the console app folder.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy a preset pack into assets/presets as NNNN.milk plus index.tsv.

The pack is a folder of "<display name>.milk" files with a manifest.json
listing {name, sha256, uses_textures, ...}. Files are renamed to plain
numbers because preset names carry characters ($, !, commas) that the
console's FTP upload and file system handle poorly, and /app0 cannot be
listed on the console, so index.tsv maps each file to its display name.
Presets that sample textures other than MilkDrop's built-in ones are left
out: projectM would draw them with a placeholder.

Usage: tools/prepare-presets.py <pack dir> [output dir]
"""

import hashlib
import json
import shutil
import sys
from pathlib import Path


# These upstream files wrap the identifier is_beat across two numbered equations.
# projectM preserves the newline, unlike the TV converter, so join only these
# known broken tokens while retaining line numbering and original line endings.
WRAPPED_BEAT = {
    "martin + stahlregen - martin in da mash 12a": 22,
    "martin [shadow harlequins shape code] - fata morgana": 23,
}


# These fractal shaders render entirely black in the validated projectM build.
# Keep their source files for future compatibility work, but omit selection.
DISABLED = {
    "martin - mandelbox explorer - high speed demo version",
    "martin, fishbrain + flexi - mandelbox explorer v1 Eo.S. optimize [bipolar witchcraft mix]",
}


def repair_equations(name: str, data: bytes) -> bytes:
    number = WRAPPED_BEAT.get(name)
    if number is None:
        return data
    ending = b"\r\n" if b"\r\n" in data else b"\n"
    before = (f"per_frame_{number}=k1 =  is_".encode() + ending +
              f"per_frame_{number + 1}=beat*equal(index%2,0);".encode())
    after = (f"per_frame_{number}=k1 =  is_beat*equal(index%2,0);".encode() + ending +
             f"per_frame_{number + 1}=".encode())
    if data.count(before) != 1:
        raise ValueError(f"preset repair no longer matches upstream: {name}")
    return data.replace(before, after, 1)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    pack = Path(sys.argv[1])
    root = Path(__file__).resolve().parents[1]
    output = Path(sys.argv[2]) if len(sys.argv) == 3 else root / "assets" / "presets"
    manifest = json.loads((pack / "manifest.json").read_text())
    chosen = []
    for entry in sorted(manifest, key=lambda item: item["name"].lower()):
        if entry.get("uses_textures"):
            continue
        source = pack / "presets" / f"{entry['name']}.milk"
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry["sha256"]:
            raise SystemExit(f"hash mismatch: {source}")
        if "\t" in entry["name"] or "\n" in entry["name"]:
            raise SystemExit(f"unusable name: {entry['name']!r}")
        chosen.append((entry["name"], repair_equations(entry["name"], data)))
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    lines = []
    for number, (name, data) in enumerate(chosen, start=1):
        file = f"{number:04d}.milk"
        (output / file).write_bytes(data)
        if name not in DISABLED:
            lines.append(f"{file}\t{name}\n")
    (output / "index.tsv").write_text("".join(lines), encoding="utf-8")
    print(f"{len(chosen)} presets in {output} ({len(lines)} enabled; {len(manifest) - len(chosen)} need textures)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
