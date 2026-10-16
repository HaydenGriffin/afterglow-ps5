# Afterglow logo

The logo is the one [Afterglow for LG webOS](https://github.com/HaydenGriffin/afterglow/tree/main/docs/brand)
uses: a poster block, AFTER over GLOW, set to one width in heavy condensed capitals (Archivo, the app's own face,
outlined and kerned by hand). The F's stem runs on down into the L, so the two lines are one piece. There is no
symbol; the block is the logo.

The SVGs here are copies of the LG masters. Change them there first and copy them across, so the two apps keep
one logo.

## Files

| File | Use |
|---|---|
| `afterglow-wordmark-ink.svg` | The block in ink, for paper and other light grounds |
| `afterglow-wordmark-paper.svg` | The block in paper, for ink and other dark grounds |
| `afterglow-icon.svg` | App icon master: ink block on a paper tile |
| `social-preview.svg`, `social-preview.png` | GitHub social preview, 1280 × 640 |

## Colour

| Name | HEX | RGB |
|---|---|---|
| Ink | `#121417` | 18 20 23 |
| Paper | `#EFEBE3` | 239 235 227 |

Ink on paper or paper on ink, nothing else. In the app, colour comes from the album artwork, not the logo.

## The PS5 home screen

| File | What it is |
|---|---|
| `sce_sys/icon0.png` | `afterglow-icon.svg` at 512 × 512: ink block on a paper tile, which holds its square on the dark home screen as it does on LG's launcher |
| `sce_sys/background-source.png` → `sce_sys/pic0.dds` | Shown while the title is selected: paper block on ink, kept to the right because the system draws the title name and Play at the lower left |
| `sce_sys/launch-background-source.png` → `sce_sys/pic1.dds` | Shown while the title starts: the block centred on ink |

The two DDS files are 3840 × 2160 BC7 without mipmaps, made from the source PNGs. `tools/prepare-assets.sh` (or
`.ps1`) does the conversion with Microsoft's `texconv`; any encoder that writes that profile works, and
`tools/validate-assets.sh` checks the result. `make app` copies `icon0.png`, `pic0.dds` and `pic1.dds` into the
title folder.

`sce_sys/snd0.at9` is the selection music of the upstream ps5-homebrew-ui kit, not Afterglow's, so the build leaves
it out and Afterglow's tile is silent on the home screen.

## Don't

Don't retype the logo in a font, set AFTERGLOW on one line, separate the F and L stems, stretch it, recolour it or
add effects to it.
