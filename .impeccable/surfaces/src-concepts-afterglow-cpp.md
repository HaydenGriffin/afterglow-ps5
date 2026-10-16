---
version: 1
slug: "src-concepts-afterglow-cpp"
primary_target: "src/concepts/afterglow.cpp"
related_targets: ["src/afterglow/panel.cpp"]
---

# Shared Afterglow TV experience

Mode: Experience with Operate controls. User explicitly chose current LG webOS Afterglow as the visual and functional reference for both releases. Code-led native adaptation; seed not applicable to this established reference.

THESIS: One music companion across TV and PS5, with uninterrupted MilkDrop and controls that remain readable from the sofa.

OWN-WORLD: Inherit the current LG Tonhalle poster system: Archivo, warm paper and ink, artwork-derived flat fields, square corners, inverse focus, no fixed accent in chrome.

STORY: Browse music, play here or follow a speaker, manage the queue, and choose visual-only, track-caption, album or lyrics presentation. Optional line-synced singalong overlays the visualiser. Settings expose display and preset controls without crowding playback.

FIRST VIEWPORT: Visuals + track is the default: a single lower-left caption over MilkDrop. Album and Lyrics use a full-height left artwork slab, oversized right-side title/current lyric, artist and next-track fields, then elapsed progress. Options reveals Browse, Now playing, Queue, Settings with controller hints and one visible focus.

FORM: Faithful adaptation of the user-selected LG composition, using native reusable ListView and controller navigation. Cached lyric parsing, real timestamp progression, honest empty states, saved options, safe 120Hz fallback, no frame-thread network work. Existing audio and preset safety remain intact.

FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance
