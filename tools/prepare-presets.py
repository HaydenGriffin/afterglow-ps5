#!/usr/bin/env python3
# Afterglow - Package MilkDrop .milk presets for the console app folder.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy the MilkDrop presets into assets/presets as NNNN.milk plus index.tsv.

The presets are the 395 LG Afterglow ships, listed with their upstream SHA-256
in tools/butterchurn-presets.tsv and taken from jberg/butterchurn-presets at a
pinned commit (fetched into .deps/butterchurn-presets unless a checkout is
given). Files are renamed to plain numbers because preset names carry
characters ($, !, commas) that the console's FTP upload and file system handle
poorly, and /app0 cannot be listed on the console, so index.tsv maps each file
to its display name, in name order. Files are numbered in the list's order, so
presets added at its end leave existing files unchanged. Presets that sample MilkDrop image textures are included:
no texture images are shipped, and the app hands projectM a generated cloud
texture for them (src/afterglow/cloud_texture.cpp), as Butterchurn substitutes
its own clouds image on LG.

Usage: tools/prepare-presets.py [butterchurn-presets checkout] [output dir]
"""

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path


URL = "https://github.com/jberg/butterchurn-presets.git"
COMMIT = "c10e2616762f262de898e5d4c1f162a3e01340c1"  # v3.0.0-beta.4


# These upstream files wrap the identifier is_beat across two numbered equations.
# projectM preserves the newline, unlike the TV converter, so join only these
# known broken tokens while retaining line numbering and original line endings.
WRAPPED_BEAT = {
    "martin + stahlregen - martin in da mash 12a": 22,
    "martin [shadow harlequins shape code] - fata morgana": 23,
    "martin - fruit machine": 25,
    "martin - sparky caleidoscope": 21,
    "martin - the forge of Isengard": 22,
}


# These use the grad3 image as a colour ramp; no image can be shipped and the
# generated clouds are no ramp, so they fade to black. Their files are kept,
# but they are not offered.
DISABLED = {
    "ORB - Planetary Alignment Acid Burn",
    "orb - toxic goo",
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


def fetch(checkout: Path) -> None:
    git = ["git", "-C", str(checkout)]
    if (checkout / ".git").is_dir():
        head = subprocess.run(git + ["rev-parse", "HEAD"], capture_output=True, text=True)
        if head.stdout.strip() == COMMIT:
            return
    shutil.rmtree(checkout, ignore_errors=True)
    checkout.mkdir(parents=True)
    subprocess.run(git + ["init", "--quiet"], check=True)
    subprocess.run(git + ["fetch", "--quiet", "--depth", "1", URL, COMMIT], check=True)
    subprocess.run(git + ["checkout", "--quiet", "FETCH_HEAD"], check=True)


def selection(root: Path) -> list[tuple[str, str]]:
    entries = []
    for line in (root / "tools" / "butterchurn-presets.tsv").read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            digest, name = line.split("\t")
            entries.append((digest, name))
    return entries


def main() -> int:
    if len(sys.argv) > 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    root = Path(__file__).resolve().parents[1]
    if len(sys.argv) >= 2:
        checkout = Path(sys.argv[1])
    else:
        checkout = root / ".deps" / "butterchurn-presets"
        fetch(checkout)
    output = Path(sys.argv[2]) if len(sys.argv) == 3 else root / "assets" / "presets"
    chosen = []
    for digest, name in selection(root):
        file = name if name.endswith(".milk") else f"{name}.milk"
        source = checkout / "presets" / "milkdrop" / file
        data = source.read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f"hash mismatch: {source}")
        if "\t" in name or "\n" in name:
            raise SystemExit(f"unusable name: {name!r}")
        chosen.append((name, repair_equations(name, data)))
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    lines = []
    for number, (name, data) in enumerate(chosen, start=1):
        file = f"{number:04d}.milk"
        (output / file).write_bytes(data)
        if name not in DISABLED:
            lines.append((name.lower(), f"{file}\t{name}\n"))
    (output / "index.tsv").write_text("".join(line for _, line in sorted(lines)), encoding="utf-8")
    print(f"{len(chosen)} presets in {output} ({len(lines)} enabled)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
