# Afterglow logo

The logo is the one [Afterglow for LG webOS](https://github.com/HaydenGriffin/afterglow/tree/main/docs/brand)
uses: *afterglow* in lowercase Archivo (the app's own face, ExtraBold, outlined and kerned by hand) whose o is the
sun, set a quarter of its height below the baseline and cut off by it. The word is the horizon and its o is
setting. The icon is a close-up of the same scene: a night sky over dark ground, the sun going down at the horizon
with two flat steps of dusk round it.

The SVGs here are copies of the LG masters. Change them there first and copy them across, so the two apps keep
one logo. The LG README has the colours, clear space, minimum sizes and don'ts.

## Files

| File | Use |
|---|---|
| `afterglow-wordmark-paper.svg` | The wordmark in paper with an amber sun, for ink and other dark grounds |
| `afterglow-wordmark-ink.svg` | The wordmark in ink, one colour, for paper and other light grounds |
| `afterglow-icon.svg` | App icon master: sky, ground, the sun and its glow |
| `afterglow-icon-small.svg` | App icon for 32 px and under: no glow, a larger sun |
| `afterglow-lockup.svg` | Icon and wordmark side by side, on ink |
| `afterglow-scene.svg` | The whole word on the horizon, sky above and ground below |
| `social-preview.svg`, `social-preview.png` | GitHub social preview, 1280 × 640 |

## Colour

| Name | HEX | RGB |
|---|---|---|
| Sky | `#16233A` | 22 35 58 |
| Sun | `#EEB875` | 238 184 117 |
| Dusk, outer and inner | `#242A47`, `#46394A` | 36 42 71, 70 57 74 |
| Ink | `#121417` | 18 20 23 |
| Paper | `#EFEBE3` | 239 235 227 |

In the app, colour comes from the album artwork, not the logo.

## The PS5 home screen

| File | What it is |
|---|---|
| `sce_sys/icon0.png` | `afterglow-icon.svg` at 512 × 512 |
| `sce_sys/background-source.png` → `sce_sys/pic0.dds` | Shown while the title is selected: the scene with the word kept to the right, because the system draws the title name and Play at the lower left |
| `sce_sys/launch-background-source.png` → `sce_sys/pic1.dds` | Shown while the title starts: the scene with the word centred |

The two DDS files are 3840 × 2160 BC7 without mipmaps, made from the source PNGs. `tools/prepare-assets.sh` (or
`.ps1`) does the conversion with Microsoft's `texconv`; any encoder that writes that profile works, and
`tools/validate-assets.sh` checks the result. `make app` copies `icon0.png`, `pic0.dds` and `pic1.dds` into the
title folder.

`sce_sys/snd0.at9` is the selection music of the upstream ps5-homebrew-ui kit, not Afterglow's, so the build leaves
it out and Afterglow's tile is silent on the home screen.
