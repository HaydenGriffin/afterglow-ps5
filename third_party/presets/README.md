# MilkDrop preset provenance

The 395 files in `assets/presets` are `.milk` files selected from
[jberg/butterchurn-presets](https://github.com/jberg/butterchurn-presets),
commit `c10e2616762f262de898e5d4c1f162a3e01340c1` (v3.0.0-beta.4), directory `presets/milkdrop`.
They are the 395 presets LG Afterglow ships. `tools/butterchurn-presets.tsv`
lists each one's upstream SHA-256 and display name; `tools/prepare-presets.py`
fetches the pinned commit, verifies every hash and writes the numbered files.
`index.tsv` retains their original names and author credits.

The upstream repository carries Jordan Berg's MIT license, reproduced in
`butterchurn-MIT.txt`. The individual presets are community works and have
no separate explicit license grant identified in that repository. Do not
infer that they are public domain or that the packaging license establishes
each author's permission. This provenance record preserves that uncertainty.

## Textures

46 of the presets name images from MilkDrop's textures folder (`worms` in 22
of them; also `cells`, `seaweed`, `manyfish`, `lichen`, `grad3`, `pic`, `sky`,
`sunrise`, `wrenches`, `smalltiled_lizard_scales`, `shub1`, `clouds2`, a random
texture and one misspelt noise sampler). No texture images are shipped: no
source found records a licence that permits redistributing them.

- projectM's [presets-milkdrop-texture-pack](https://github.com/projectM-visualizer/presets-milkdrop-texture-pack)
  (checked at `6368812f27bc747b517218fbf89d21d59afce4d9`) has no license file
  and no licence statement; its README says only that it holds the textures
  originally released with MilkDrop plus others used by presets.
- MilkDrop 2's source release (Nullsoft, BSD 3-Clause; checked in the
  projectM-visualizer/milkdrop2 mirror of the SourceForge repository) does
  not include the textures.
- butterchurn-presets embeds six of these images (`cells`, `seaweed`,
  `smalltiled_lizard_scales`, `prayerwheel`, `lichen` and a clouds image) in
  `imageData.js`. The repository's MIT notice is Jordan Berg's copyright; the
  images come from MilkDrop and no grant from their authors is recorded.

LG Afterglow does not load images either: Butterchurn draws its built-in clouds
image for any texture a preset names that it has not loaded. Afterglow does the
same with a cloud texture it generates itself (`src/afterglow/cloud_texture.cpp`,
128 x 128 RGBA, 64 KiB per texture name projectM is asked for). Two presets
use the `grad3` image as a colour ramp and fade to black without it, so they
are kept as files but left out of `index.tsv`: `ORB - Planetary Alignment Acid
Burn` and `orb - toxic goo`. There are 393 enabled presets.

## Repairs

Five presets have narrowly scoped equation repairs: `martin + stahlregen -
martin in da mash 12a`, `martin [shadow harlequins shape code] - fata morgana`,
`martin - fruit machine`, `martin - sparky caleidoscope` and `martin - the
forge of Isengard`. Each original splits `is_beat` across two numbered lines;
projectM reads that as two identifiers and rejects the preset.
`tools/prepare-presets.py` verifies the original source hash before joining
those tokens, preserving line numbers and line endings. The other 390 presets
remain byte-for-byte upstream originals.

The two Mandelbox explorer presets, which used to render black, are fixed in
projectM rather than in the preset text: see `uniform-copy.patch` in
`third_party/projectm`.
