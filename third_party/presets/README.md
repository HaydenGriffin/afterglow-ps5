# MilkDrop preset provenance

The 349 files in `assets/presets` are `.milk` files selected from
[jberg/butterchurn-presets](https://github.com/jberg/butterchurn-presets),
commit `c10e2616762f262de898e5d4c1f162a3e01340c1` (v3.0.0-beta.4), directory `presets/milkdrop`.
`index.tsv` retains their original names and author credits. The other 46
presets in the 395-file source selection require external image textures
and are omitted. `tools/prepare-presets.py` verifies source SHA-256 hashes.

The upstream repository carries Jordan Berg's MIT license, reproduced in
`butterchurn-MIT.txt`. The individual presets are community works and have
no separate explicit license grant identified in that repository. Do not
infer that they are public domain or that the packaging license establishes
each author's permission. This provenance record preserves that uncertainty.
No texture pack is included.

Two presets have narrowly scoped equation repairs: `martin + stahlregen - martin
in da mash 12a` and `martin [shadow harlequins shape code] - fata morgana`.
Each original splits `is_beat` across two numbered lines; projectM reads that as
two identifiers and rejects the preset. `tools/prepare-presets.py` verifies the
original source hash before joining those tokens, preserving line numbers and
line endings. The other 347 presets remain byte-for-byte upstream originals.

Two Mandelbox presets (`0241.milk` and `0259.milk`) are retained as source but
omitted from the selectable index: both render completely black with the pinned
projectM build, including a 210-frame host check. There are 347 enabled presets.
