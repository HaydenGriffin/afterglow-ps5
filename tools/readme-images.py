# Afterglow - Compose the README pictures from the off-screen preview.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes docs/images/hero.jpg and docs/images/views.jpg from preview captures.

Render the captures first, with the invented cover and karaoke lyrics:

    AFTERGLOW_PREVIEW=1 AFTERGLOW_COVER=docs/images/preview-cover.png \\
      AFTERGLOW_LYRICS=karaoke tools/host-snapshots.sh build/readme afterglow
    python3 tools/readme-images.py build/readme

Needs Pillow.
"""

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
HERO = "lyrics"
VIEWS = ("album", "idle-recent", "browse", "players")


def capture(folder: Path, name: str) -> Image.Image:
    matches = sorted(folder.glob(f"*-afterglow-{name}.png"))
    if not matches:
        raise SystemExit(f"no capture named {name} in {folder}")
    return Image.open(matches[0]).convert("RGB")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: tools/readme-images.py <capture folder>")
    folder = Path(sys.argv[1])
    out = ROOT / "docs/images"
    capture(folder, HERO).save(out / "hero.jpg", quality=88, optimize=True)
    cell = (960, 540)
    sheet = Image.new("RGB", (cell[0] * 2, cell[1] * 2))
    for index, name in enumerate(VIEWS):
        picture = capture(folder, name).resize(cell, Image.LANCZOS)
        sheet.paste(picture, ((index % 2) * cell[0], (index // 2) * cell[1]))
    sheet.save(out / "views.jpg", quality=88, optimize=True)
    print(f"wrote {out / 'hero.jpg'} and {out / 'views.jpg'}")


if __name__ == "__main__":
    main()
