# Afterglow - Compose the README pictures from console or preview captures.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes docs/images/hero.jpg and docs/images/views.jpg from captures.

The pictures in the README are console screenshots (tools/afterglow-dev.py
screenshot, converted to PNG) saved in one folder as lyrics.png, album.png,
overlay-line.png, overlay-stage.png and display.png. The off-screen preview
works too: its captures are found by the same names.

    python3 tools/readme-images.py <capture folder>

Needs Pillow.
"""

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
HERO = "lyrics"
VIEWS = ("album", "overlay-line", "overlay-stage", "display")
GAP = 16


def capture(folder: Path, name: str) -> Image.Image:
    matches = sorted(folder.glob(f"{name}.png")) or sorted(folder.glob(f"*-afterglow-{name}.png"))
    if not matches:
        raise SystemExit(f"no capture named {name} in {folder}")
    return Image.open(matches[0]).convert("RGB")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: tools/readme-images.py <capture folder>")
    folder = Path(sys.argv[1])
    out = ROOT / "docs/images"
    capture(folder, HERO).save(out / "hero.jpg", quality=88, optimize=True)
    cell = ((1920 - GAP) // 2, (1080 - GAP) // 2)
    sheet = Image.new("RGB", (1920, 1080))
    for index, name in enumerate(VIEWS):
        picture = capture(folder, name).resize(cell, Image.LANCZOS)
        sheet.paste(picture, ((index % 2) * (cell[0] + GAP), (index // 2) * (cell[1] + GAP)))
    sheet.save(out / "views.jpg", quality=88, optimize=True)
    print(f"wrote {out / 'hero.jpg'} and {out / 'views.jpg'}")


if __name__ == "__main__":
    main()
