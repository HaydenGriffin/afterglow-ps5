# Third-party notices

Afterglow for PS5 is GPL-3.0-or-later. It is built on BlackBearReloaded's
[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) (GPL-3.0-or-later), whose own notices
follow below. This file lists everything third party in the repository and says which of it the PS5 app folder
(`dist/PPSA99481`, the release ZIP) actually ships. A copy of this file, `LICENSE` and the licence texts below are
in the app folder's `licenses/` directory.

## What the app folder ships

| In the app folder | What it is | Licence | Texts in `licenses/` |
| --- | --- | --- | --- |
| `eboot.bin` | Afterglow and the ps5-homebrew-ui kit | GPL-3.0-or-later | `LICENSE` |
| `eboot.bin` (linked in) | [projectM](https://github.com/projectM-visualizer/projectm) and its bundled dependencies | LGPL-2.1-or-later; MIT, public domain and others (below) | `projectm/` |
| `eboot.bin` (linked in) | [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) 1.0.2 with Mesa and OpenGNM PSBC | GPL-3.0-or-later; Mesa mostly MIT, per file; OpenGNM MIT | `ps5-opengl/` |
| `eboot.bin` (linked in) | [jsmn](https://github.com/zserge/jsmn) | MIT | `jsmn/` |
| `eboot.bin` (linked in) | [BearSSL](https://www.bearssl.org/) (HTTPS for online lyrics) | MIT | `bearssl/` |
| `eboot.bin` (linked in) | [stb_vorbis](https://github.com/nothings/stb) | MIT or public domain | `stb/` |
| `eboot.bin` (linked in) | Parts of LLVM libc++, libc++abi, libunwind and compiler-rt builtins, from the PS5 Payload SDK and the compiler | Apache-2.0 WITH LLVM-exception | none needed in binary form |
| `eboot.bin` (linked in) | Three C-locale objects of the PS5 Payload SDK's `libc.a` (`no-locale.o`, `mbsnrtowcs.o`, `wcsnrtombs.o`), FreeBSD-derived | See below | see below |
| `sce_module/libc.prx` | The project's clean-room runtime shim | GPL-3.0-or-later | `LICENSE` |
| `assets/fonts/archivo-*.huifont` | Archivo, baked to distance fields | SIL OFL 1.1 | `fonts/Archivo-LICENSE.txt` |
| `assets/fonts/dejavu-sans-mono.huifont` | DejaVu Sans Mono, baked | Bitstream Vera licence; DejaVu changes public domain | `fonts/DejaVu-LICENSE.txt` |
| `assets/presets/` | 395 MilkDrop presets from butterchurn-presets | No explicit licence for the presets themselves (below) | `presets/` |
| `sce_sys/icon0.png`, `pic0.dds`, `pic1.dds` | Afterglow's logo on the home screen | GPL-3.0-or-later, with the project | `LICENSE` |

Not shipped, though in the repository: the upstream kit's sound effects, music and selection music
(`assets/audio/`, `sce_sys/snd0.at9`), the Inter, Montserrat, Press Start 2P and Patrick Hand fonts, the gallery of
designs, and the host-only tools (stb_truetype, stb_image_write, GoogleTest, zlib).

The FreeBSD-derived `libc.a` objects are linked so projectM's locale functions resolve; FreeBSD's libc is under
BSD-style licences, which ask for the copyright notice in binary distributions.
<!-- TODO(owner): find the exact copyright lines of no-locale.c, mbsnrtowcs.c and wcsnrtombs.c in the PS5
     Payload SDK's libc sources and add them here (and as a text file under licenses/). -->

### projectM and the LGPL

projectM is statically linked into `eboot.bin`, so the LGPL-2.1 terms for a "work that uses the Library" apply:

- The projectM source used is upstream commit `349b7cc2db312d47cef6be24c6856ced6643a385` of
  <https://github.com/projectM-visualizer/projectm> with its projectm-eval submodule, plus the patches and adapters
  in `third_party/projectm/` of this repository; `tools/build-projectm.sh` fetches, patches and builds it.
- Everything needed to relink the application against a modified projectM is this repository: the complete
  application source, the build scripts, and the same pinned toolchain. `make app` relinks it.
- These notices and projectM's licence text ship with every build.

<!-- TODO(owner): for strict LGPL-2.1 section 6 compliance when publishing binaries, consider attaching the patched
     projectM source (an archive of .deps/projectm/src after tools/build-projectm.sh) to each GitHub release, so the
     library source is offered from the same place as the ZIP. -->

### The presets' licence, plainly

The 395 presets are community works collected in
[jberg/butterchurn-presets](https://github.com/jberg/butterchurn-presets). That repository carries Jordan Berg's MIT
licence for the collection, but no explicit licence grant from the individual preset authors has been found. Don't
read them as public domain, or as covered by this project's licence. Author names are kept in
`assets/presets/index.tsv`; details are in `third_party/presets/README.md`. If you are a preset's author and want it
credited differently or removed, open an issue.

## Upstream notices (ps5-homebrew-ui)

### Credits and acknowledgements

ps5-homebrew-ui exists thanks to the maintainers and contributors of:

- [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl), with
  [Mesa](https://mesa3d.org/) and OpenGNM PSBC, for OpenGL 4.6 on PS5;
- [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  and the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) for the
  reproducible native foundation;
- [Inter](https://github.com/rsms/inter),
  [Montserrat](https://github.com/JulietaUla/Montserrat),
  [DejaVu](https://dejavu-fonts.github.io/),
  [Press Start 2P](https://fonts.google.com/specimen/Press+Start+2P) and
  [Patrick Hand](https://fonts.google.com/specimen/Patrick+Hand) for the
  interface typefaces, and
  [stb](https://github.com/nothings/stb) for music decoding, font baking and
  PNG writing;
- LLVM/Clang, Python, zlib and GoogleTest for build, packaging and validation
  tooling;
- Inigo Quilez, whose published signed-distance functions the shape shader
  follows (rounded box, triangle, star and arc).

The rendering kit, the sound mixer and the sound effects come from
[ProsperoPuzzles](https://github.com/blackbearreloaded/ProsperoPuzzles) and
ProsperoEden, both by BlackBearReloaded.

ps5-homebrew-ui is Copyright (C) 2026 BlackBearReloaded and licensed under
GPL-3.0-or-later. It is built on `ps5-native-app-boilerplate`; the notices
below cover the boilerplate's dependencies and every third-party component the
project adds.

### Fonts and font baking

| Typeface | Files | Copyright | Licence |
| --- | --- | --- | --- |
| Inter Regular, SemiBold | `third_party/fonts/Inter-*.ttf` | (c) 2016 The Inter Project Authors | SIL Open Font License 1.1 (`Inter-LICENSE.txt`) |
| Montserrat Medium | `third_party/fonts/Montserrat-Medium.ttf` | (c) 2011 The Montserrat Project Authors | SIL Open Font License 1.1 (`Montserrat-LICENSE.txt`) |
| DejaVu Sans Mono | `third_party/fonts/DejaVuSansMono.ttf` | (c) 2003 Bitstream, Inc.; DejaVu changes are public domain | Bitstream Vera licence (`DejaVu-LICENSE.txt`) |
| Press Start 2P | `third_party/fonts/PressStart2P-Regular.ttf` | (c) 2012 The Press Start 2P Project Authors | SIL Open Font License 1.1 (`PressStart2P-LICENSE.txt`) |
| Patrick Hand | `third_party/fonts/PatrickHand-Regular.ttf` | (c) 2010-2012 Patrick Wagesreiter | SIL Open Font License 1.1 (`PatrickHand-LICENSE.txt`) |

`assets/fonts/*.huifont` are distance-field renderings of these typefaces
produced by `tools/bake-fonts.sh`; the licence texts ship beside them.
Afterglow's app folder ships only DejaVu Sans Mono from this table, with
Archivo (below).

The baker uses [stb_truetype](https://github.com/nothings/stb)
(`third_party/stb/stb_truetype.h`, public domain or MIT) and the host snapshot
tool uses stb_image_write (`third_party/stb/stb_image_write.h`, same terms).
Both are host-only and are not linked into the PS5 application.

### Sound effects and music

`assets/audio/sfx/paper/*.wav` are the sound effects of ProsperoPuzzles and
`assets/audio/sfx/glass/*.wav` those of ProsperoEden. They were generated for
those projects with ElevenLabs Sound Effects v2 and prepared with
`tools/process-sfx.py` (trimmed, faded and levelled). No sound effect was
generated for this repository. `assets/audio/music/*.ogg` ("First Light",
"Open Strings" and "Quiet Hours") are the project's own background music,
supplied by its owner and encoded to 48 kHz OGG Vorbis at -18 LUFS. All are
Copyright (C) 2026 BlackBearReloaded and distributed under the project licence.
Afterglow does not package any of them.

### Music decoding

Music is decoded by [stb_vorbis](https://github.com/nothings/stb) (public
domain or MIT; see `src/third_party/stb/LICENSE`), vendored at the commit in
`src/third_party/stb/UPSTREAM` by `tools/update-stb.sh`. It is linked into the
PS5 application.

### Shader distance functions

The fragment shader in `src/gfx/gl_batch.cpp` adapts the two-dimensional
signed-distance functions published by Inigo Quilez
(https://iquilezles.org/articles/distfunctions2d/), which he releases under
the MIT licence.

### OpenGL runtime

The PS5 build statically links the
[ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) SDK
(GPL-3.0-or-later), which contains Mesa (MIT) and OpenGNM PSBC components under
their own licenses. The SDK is fetched or selected at build time and is not
stored in this repository. Its license texts (`LICENSE`, `THIRD_PARTY_NOTICES.md`
and `LICENSES/`, including Mesa's per-file licences) ship inside the SDK archive,
and `tools/build.sh` copies them into the app folder's `licenses/ps5-opengl/`.

### Native build dependencies

The application build uses LLVM/Clang/lld, zlib 1.3.2, and the public
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk). The bootstrapper
downloads SDK v0.42 after verifying SHA-256
`8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
It downloads zlib 1.3.2 from the upstream source archive after verifying
SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
and compiles its static archive locally. Both dependencies remain under ignored
`.deps/native/`, retain their upstream licenses, and are not distributed by
this repository. No Sony SDK file is included.

Target C++ compilation uses the LLVM libc++ headers distributed by the public
SDK. Those headers retain the Apache-2.0 WITH LLVM-exception license recorded
upstream. The link group `tools/prepare-opengl.sh` writes pulls the parts of the
SDK's `libc++.a`, `libc++abi.a` and `libunwind.a`, and the compiler's builtins,
that ps5-opengl and projectM use into `eboot.bin`; the LLVM exception waives the
notice requirement for such compiled forms. The complete archives are not
redistributed.

The PS5 ELF converter and FSELF writer in `tooling/native/` are derived from
[SharpProspero](https://github.com/SvenGDK/SharpProspero), Copyright (C) 2026
SvenGDK, GPL-3.0, and were translated to C++ and modified by BlackBearReloaded.

### Host test dependency

The host unit-test target downloads
[GoogleTest](https://github.com/google/googletest) 1.17.0 after verifying
SHA-256 `65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c`.
It remains under ignored `.deps/test/`, retains its BSD-3-Clause license, and
is not linked into any PS5 application, runtime, or package artifact.

### Optional PacBrew dependencies

When selected through `PACBREW_*` build variables, the build downloads the prebuilt ports image
from [ps5-payload-dev/pacbrew-repo](https://github.com/ps5-payload-dev/pacbrew-repo)
release `v0.40.2`, verifies its published SHA-256, and extracts only the
`target/user/homebrew` prefix under ignored `.deps/pacbrew/`. It does not
replace the pinned SDK or install files globally. PacBrew recipes and every
linked third-party library retain their upstream licenses; applications must
review those terms before redistribution.

### Independently authored runtime shim

`tooling/native/libc_builder.cpp` and the manifests under
`tooling/native/runtime/` are independently authored for this project and
licensed under GPL-3.0-or-later. The generated `runtime/libc.prx` contains
project-authored compatibility stubs, startup code, and semantic loader
metadata. It contains no Sony runtime implementation.

Original ps5-native-app-boilerplate code is Copyright (C) 2026
BlackBearReloaded and licensed under GPL-3.0-or-later. Source and script files
carry matching SPDX identifiers.

### Presentation assets

`sce_sys/snd0.at9`, the music the console plays while the title is selected, is
ps5-homebrew-ui's owner's own track, encoded with
[ps5-at9-converter](https://github.com/blackbearreloaded/ps5-at9-converter)
(57.6 s, 192 kb/s, -28 LUFS, whole-track loop). It stays in the repository but
Afterglow does not package it. Afterglow replaced upstream's icon and
backgrounds with its own (see "Afterglow's own artwork" below).
`HUI_ICON=<file> tools/host-snapshots.sh` still renders a plain icon
with the project's own renderer, for forks that want one of their own. The
gallery's cover art is rendered at start-up from invented titles; no real
product's name, artwork or trademark is used.

No proprietary runtime module, encryption key, or game file is included.

## Afterglow's dependencies

<!-- TODO(owner): the lyrics lane fetches LRCLIB over HTTPS; if it adds a TLS library (or any other code), add
     it to the table at the top, here, and to licenses/ through tools/build.sh. -->

### jsmn

`third_party/jsmn/jsmn.h`: jsmn JSON tokenizer by Serge Zaitsev, MIT license.
Source: https://github.com/zserge/jsmn at commit
`25647e692c7906b96ffd2b05ca54c097948e879c`. The license text is retained in the
header and in `third_party/jsmn/LICENSE`, which ships as `licenses/jsmn/LICENSE`.

### projectM / MilkDrop

[projectM](https://github.com/projectM-visualizer/projectm) is pinned to
`349b7cc2db312d47cef6be24c6856ced6643a385` (4.2 development API).
Copyright 2003–2026 projectM contributors, LGPL-2.1-or-later; see
`third_party/projectm/licenses/projectM-LGPL-2.1.txt`.
`tools/build-projectm.sh` fetches the pinned source and projectm-eval submodule.
It builds static archives with exception support, a linked-EGL resolver, PS5
locale/filesystem compatibility, and a PS5-only visual random seed adapter.
The source patch and adapters are retained in `third_party/projectm`.
The app's source and build scripts provide the relinking path.

Bundled upstream dependencies include projectm-eval (MIT, projectM Team),
hlslparser (MIT, Unknown Worlds Entertainment), GLM (MIT), stb_image
(MIT or public domain), glad ((WTFPL or CC0-1.0) and Apache-2.0), and
Jonathan Dummer's public-domain DXT routines. License texts and upstream
notices are in `third_party/projectm/licenses`; source-level notices remain
in the pinned source tree.

The 395 bundled community presets retain author names in `assets/presets/index.tsv`.
Their origin and the limits of the available licensing information are recorded
in `third_party/presets/README.md`, alongside the upstream repository's MIT text.

No MilkDrop texture images are bundled. The images some presets sample (from
MilkDrop's textures folder, as collected in projectM's
presets-milkdrop-texture-pack and in butterchurn-presets' image data) carry no
licence that permits redistribution; see `third_party/presets/README.md`. Those
presets get a cloud texture the app generates itself
(`src/afterglow/cloud_texture.cpp`, GPL-3.0-or-later).

### BearSSL (online lyrics)

[BearSSL](https://www.bearssl.org/) by Thomas Pornin, MIT license
(`third_party/bearssl/LICENSE.txt`, also shipped as `licenses/bearssl/LICENSE.txt`
in the app folder). `tools/build-bearssl.sh` fetches it from
`https://www.bearssl.org/git/BearSSL` at commit
`7bea48e5e850ab4cafbe68d3765cdaba13a86d6f` (the commit id pins the whole tree),
builds it under ignored `.deps/bearssl/`, and statically links it into the PS5
application and the host preview. It carries the HTTPS requests of the online
lyrics search to lrclib.net. On the console its OS hooks are off: randomness
comes from the CPU's RDRAND instruction and the time from the app. No source
is modified and none is stored in this repository.

Certificates are checked against the console's own CA list
(`/system/common/cert/CA_LIST.cer`) or, on a PC, the system's bundle; none is
shipped.

### Archivo

Archivo by Omnibus-Type, SIL Open Font License 1.1. Static instances (600/100, 750/88 and 820/80 weight/width) were made from the existing LG Afterglow bundled variable font. Source and licence: `third_party/fonts/Archivo-*.ttf` and `Archivo-LICENSE.txt`. Baked atlases ship in `assets/fonts`.

### Afterglow's own artwork

`sce_sys/icon0.png`, `sce_sys/pic0.dds` (shown while the title is selected) and
`sce_sys/pic1.dds` (shown while it starts), with their sources
`background-source.png` and `launch-background-source.png`, and everything in
`docs/brand/` show the Afterglow logo, drawn for Afterglow for LG webOS from
Archivo's letterforms (outlined and kerned by hand). They are the project's own
and come with it under GPL-3.0-or-later. `docs/images/preview-cover.png` and the
README pictures are invented: the preview's songs, artists and cover are made up.

### Online services

When Music Assistant has no lyrics, Afterglow asks [LRCLIB](https://lrclib.net)
for them. No LRCLIB code or data is bundled.
