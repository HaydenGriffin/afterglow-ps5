# Contributing

Bug reports and pull requests are welcome. New to the repository? Start with [AGENTS.md](AGENTS.md) (it is written
for people too), [docs/AFTERGLOW.md](docs/AFTERGLOW.md) for how Afterglow works, and [DESIGN.md](DESIGN.md) for how it
looks. Afterglow follows [Afterglow for LG webOS](https://github.com/HaydenGriffin/afterglow) in behaviour and wording;
a change that makes the two differ should say why.

Before committing a change:

1. Run `make test` and `make lint`; add a focused unit or integration regression for behaviour changed by the patch.
   A change to what is on screen comes with the preview pictures you looked at
   (`AFTERGLOW_PREVIEW=1 tools/host-snapshots.sh build/afterglow-preview afterglow`).
2. Run `make`; it must reproduce the clean-room `runtime/libc.prx` digest.
3. Confirm the build reports zero static FSELF errors.
4. Do not commit `.env`, `afterglow.conf`, `build/`, `dist/`, `.local/`, `results/`, proprietary PRXs, game files, SDK
   binaries, generated `runtime/libc.prx`, console dumps, keys, tokens, credentials, or your own network's addresses
   (examples use `192.0.2.x`).
5. Include the firmware and homebrew enabler for platform-specific behavioural claims, and say plainly what was not
   checked on a console.
6. Release tags are `v` and the version (`v1.6.0`), matching `src/afterglow/version.hpp` and
   `sce_sys/param.json`'s `contentVersion` (`01.006.000`); add the version's section to [CHANGELOG.md](CHANGELOG.md)
   first, because the release notes are taken from it.

Every comment-capable file must start with a header like this (adapt the comment syntax and the one-line
description). Files from upstream keep their original header:

```cpp
// Afterglow - One-line description of the file.
// Copyright (C) 2026 Afterglow contributors
// SPDX-License-Identifier: GPL-3.0-or-later
```

Vendored upstream code under `src/third_party/` and `third_party/` keeps its upstream license header and is listed in
`THIRD_PARTY_NOTICES.md`.

Changes to `tooling/native/` must include a deterministic host check and a narrowly scoped static-format regression.
Loader-visible changes also require hardware results before release.
