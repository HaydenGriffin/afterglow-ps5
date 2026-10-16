---
name: Afterglow PS5
description: Native adaptation of the LG Afterglow concert poster for a TV and DualSense.
colors:
  ink: "#121417"
  paper: "#efebe3"
  muted-paper: "rgba(239,235,227,0.68)"
  rule: "rgba(239,235,227,0.22)"
  surface-high: "#292b2e"
  amber: "#eeb875"
  mint: "#87cabb"
typography:
  display-short:
    fontFamily: Archivo
    fontSize: 184px
    lineHeight: 0.94
  display:
    fontFamily: Archivo
    fontSize: 144px
    lineHeight: 0.94
  display-long:
    fontFamily: Archivo
    fontSize: 104px
    lineHeight: 0.94
  display-xlong:
    fontFamily: Archivo
    fontSize: 80px
    lineHeight: 0.94
  lyric-now:
    fontFamily: Archivo
    fontSize: 88px
    lineHeight: 1
  field-title:
    fontFamily: Archivo
    fontSize: 52px
  caption-title:
    fontFamily: Archivo
    fontSize: 58px
  nav:
    fontFamily: Archivo
    fontSize: 30px
  body:
    fontFamily: Archivo
    fontSize: 28px
  supporting:
    fontFamily: Archivo
    fontSize: 26px
  label:
    fontFamily: Archivo
    fontSize: 24px
rounded:
  none: 0px
  spectrum-bar: 8px
  scan-notice: 12px
spacing:
  screen-inset: 96px
  field-inset: 28px
  panel-inset: 48px
  field-gap: 24px
components:
  canvas:
    backgroundColor: "{colors.ink}"
    width: 1920px
    height: 1080px
  poster-title:
    textColor: "{colors.paper}"
    typography: "{typography.display}"
    width: 1144px
  sheet:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.paper}"
    rounded: "{rounded.none}"
    width: 1264px
    height: 1080px
  row-focus:
    backgroundColor: "{colors.paper}"
    textColor: "{colors.ink}"
    rounded: "{rounded.none}"
    height: 76px
  track-caption:
    typography: "{typography.caption-title}"
    rounded: "{rounded.none}"
    width: 1144px
  spectrum-bar:
    rounded: "{rounded.spectrum-bar}"
    width: 16px
---

# Design System: Afterglow PS5

## Overview

**Creative North Star: "The Tonhalle Poster"**

Afterglow PS5 adapts the user-selected current LG webOS Afterglow: a heavy Archivo title, full-height cropped artwork, warm paper on ink and flat track-colour fields. The music owns the screen; controls stay readable from the sofa and MilkDrop retains its own visual language.

This records the implemented native adaptation, not exact LG CSS parity. The authority is the LG DESIGN.md and the native source; the retained upstream gallery is outside this surface. The previous Inter/Montserrat chrome and six-section information panel are superseded.

**Key Characteristics:**
- Archivo poster typography and a full-height left artwork slab.
- Flat square fields, warm paper focus inversion and no fixed chrome accent.
- Four display modes; Options opens a transport band (section strip and eight tiles) that leads to Browse, Queue, Settings and Home sheets.
- A single caption field over the visualizer, optionally carrying a timed lyric.

## Colors

Ink and Paper are the fixed chrome neutrals; muted paper carries secondary text and Rule carries hairlines. Panel focus fills with Paper and reverses text to Ink. The internal panel constant named kMint now resolves to Paper.

Field A comes from the native artwork colour, with readable_field choosing ink or paper at a target contrast of 4.5:1 and darkening in 15% steps when needed. Field B comes from the secondary artwork colour (the accent × 0.45), contrast checked the same way; if it sits within 1.25:1 of A it falls back to A × 0.55. These are runtime colours, not fixed brand accents or LG's full palette-role extraction. Before artwork decodes the fields are neutral greys (#3e4248 and #26292e), never a white slab on an OLED.

Amber and Mint remain restricted to the halo/Spectrum visualization and reference identity metadata. They are not active navigation colours. MilkDrop keeps preset colours unless album colour is enabled; then the preset's waveform, border and motion-vector slots take the artwork hue at the preset's own brightness (LG viz-man's slot recolouring), blended by the 25-100 % strength. Halo and Spectrum lift slightly on detected beats, never under reduced motion.

**The No House Accent Rule.** Chrome emphasis uses Paper; track fields take their colour from artwork.

## Typography

The native entry point loads archivo-regular, archivo-semibold and archivo-display baked font atlases; generic FontRef comments mentioning Inter/Montserrat are stale toolkit labels. Archivo carries the current interface. The bundled mono font is not the poster voice. Native font instances replace LG's browser variable-font stack; no CSS fallback or live variation axis is implemented.

The Album title uses a fixed length ladder: 184 at string length ≤12, 144 at ≤26, 104 at ≤50, otherwise 80 logical pixels. Native string length is the source's byte-count test. Titles wrap to at most three lines in 1144 pixels with 0.94 line spacing and final baseline 608. This intentionally records native behavior rather than LG's two-line rule. Secondary metadata is fitted to its available single-line width.

Timed Lyrics uses an 88-pixel current line, with earlier/later lines at 32/36 and 68% paper. Plain Lyrics uses 44-pixel semibold text on 52-pixel lines, each entry up to two lines, paged by measured height (360 pixels per page). Caption titles use 58 pixels, or 64 for singalong; field titles use 52. Navigation is 30, body metadata 28, supporting information 26 and hints 24. Text uses native baseline positioning and default zero tracking.

## Layout

Fixed 1920 × 1080 logical canvas; MilkDrop renders at 1280 × 720 and fills it. No mobile breakpoints are implemented. Album/Lyrics place cropped artwork at (0,0,656,1080), content from x=680 to 1824, target/status baseline 92 and a rule at y=124. Field A is (680,670,706,248), Field B (1410,670,414,248); progress runs at y=970. The cover crop uses UV (0.196,0,0.608,1).

Visuals + track places a 1144-pixel caption at x=96 with bottom y=980, 28-pixel horizontal inset and height determined by up to two caption lines. Visuals only hides this field. Display hints are a right-aligned glyph row (30-pixel glyphs, 24-pixel labels, centre y=1000, ending at x=1814), on an ink plate over the visuals; plain Lyrics adds D-pad Lyrics.

Options opens the transport band rather than a sheet. A 108-pixel ink strip slides down from the top with the sections (Browse, Now playing, Queue, Settings, and Home once configured), the current one ticked and the focused one filled with paper; L1/R1 Sections and Circle Close hints sit at its right. An ink band slides up to y=810 with the playing title · artist (26 px) and elapsed / duration, then eight 192 × 119 tiles 24 pixels apart from x=96: Previous, Play/Pause, Next, Back 10 s, Ahead 10 s, Favourite, Details, Display. Each tile is a 40-pixel drawn icon over a 26-pixel label under a two-pixel rule; focus fills it with paper. The band does not cover or pause the visuals.

Sheets (Queue, Settings and its pages, Details, Display, Home) are a full-height ink panel from x=656, beside the poster's artwork slab, with text from x=680 to 1824: a 65-pixel title on baseline 118, an optional note on 160, a rule at 164 (188 with a note), 76-pixel rows and 64-pixel headers down to y=936, a notice line on 966 and a DualSense glyph footer (34-pixel glyphs, centre y=1000) that changes with the page and focused row. Browse, search, artist and collection pages cover the screen: a 97-pixel title on baseline 142, a category column (63-pixel rows) from x=96 to 515 and the collection from x=539 (104-pixel rows; 86 for item actions). Sound & sync adds a sync figure placing "Picture & lyrics" against "The music" on a ±600 ms scale. Open sheets pause MilkDrop; Album and Lyrics do not unless "MilkDrop behind album and lyrics" is off.

## Elevation & Depth

Flat fills and stacking order create depth, without blur or shadows. The poster and controls are opaque ink. Picture brightness darkens the visual layer; text brightness currently adds an ink overlay over the composed poster/caption before the controls panel is drawn. From 22:00 to 06:00 local time the night register (on by default) steps paper down to #c4c0b9, secondary text to 72 % of it, the artwork to 72 % opacity and the fields to 72 % of their colour, as LG's player.css does.

Chrome opacity approaches its target by 1 − exp(−12 × dt), with drawing stopped below 0.01. The At track changes card hides the caption after nine seconds. OLED movement shifts the poster/caption by a sinusoidal six pixels; reduced motion freezes MilkDrop on its current frame (redrawn only on a preset change), stops the drift phase and halo drift, and skips beat lifts. On a song change a ground-coloured cover slides off each poster field toward the right (0.52 s, cubic-bezier(.16,1,.3,1); field B 70 ms after A); reduced motion skips it.

## Shapes

Poster fields, controls, focused rows and hints have square corners. Hairlines are generally two pixels; active navigation has a three-pixel underline. The visualizer is separate: Spectrum retains rounded bars, halo retains three elliptical outlines, and the performance-scan notice retains its rounded native shape. These exceptions are not a rounded-card chrome vocabulary.

## Components

**Navigation and lists.** Options opens the transport band; L1/R1 step through Browse, Now playing (the band itself), Queue, Settings and Home, wrapping. The Details tile opens the back of the poster (facts, music favourite, start radio); the Display tile opens the display sheet (modes and lyrics layer with ticks, MilkDrop behind posters, track card, visual style, reduce motion, word-by-word lyrics). Queue holds Playing on, the 50-song up-next window with Earlier/Later songs, and the playback rows (shuffle, repeat, crossfade, volume, sleep timer, autoplay). Settings nests Source, Visuals & presets (grouped Picture / Presets / Performance / Controller light), Lyrics timing, Presets, Sound & sync, Account and Status. Every row names its action, so reordering a page never changes behaviour. Adjustable rows show stroked chevrons around the value only while focused (the Archivo faces carry no single angle quotes); rows that open a page carry a trailing chevron. Left/right only changes adjustable values; toggles and actions take Cross. Row focus is a full paper fill with ink text, with no breathing or horizontal shift. On the poster and over visuals, controller hints are glyphs that fade six seconds after the last input or track change, and L2/R2 Preset appears only while MilkDrop is shown.

**Poster and fields.** Album gives the title the large right zone, artist/album Field A, the next two songs in Field B and elapsed/duration progress below. The status line at the top carries the quality badge when Music Assistant supplies one, otherwise the format. Lyrics swaps the title for real timed lines, manually paged untimed text or an explicit unavailable message. Artwork only draws when a Music Assistant texture exists; the field remains as its backing.

**Idle poster.** With nothing playing for 8 s (30 s after a pause) a full-screen poster replaces the display mode, as LG's #idle: target name and Paused / Nothing playing on baseline 92 over the rule at 124, a 544-pixel cover box at (96,173), the title on the length ladder with its last baseline at 608, Field A at (680,670,706,248) holding the item's detail (40 px semibold) and a Cross glyph hint, and a strip of 88-pixel covers from x=680 at y=936 that ends in a Browse word tile; focus fills a tile with paper. Only the paused song wears its colours; recent items, Browse and Try again sit on the neutral fields. After three quiet minutes an ink veil dims everything to 35 % (eased, immediate under reduced motion); the night register and OLED movement apply.

**Visual caption and singalong.** Visuals + track is the saved default. A single flat field carries the title and artist/album. With singalong enabled and a current timed line available, that line replaces the large title and title/artist stays below. Untimed lyrics never receive invented timestamps. With karaoke on and enhanced LRC word timings, the current line lights word by word (unsung words at 42 % of its opacity) on the Lyrics poster, the one-line field and the stage. Visuals only can still briefly show help and the preset name.

**Presets and settings.** Presets support loading, R3 favourite and Square Keep; L2/R2 selects presets outside the panel (lyric timing on the timed Lyrics poster) and L3 locks automatic changes. Visuals & presets exposes brightness, album colour and strength, night register, duration, order, favourites, beat sensitivity, frame caps, performance scanning and the light bar. A 120 fps choice requests 120 Hz on the next launch, falls back to 60 when unsupported, and reports accepted refresh; it is not a hardware performance claim.

**State and provenance.** Missing lyrics, empty queues, disconnected sources and unavailable actions use explicit text. Metadata is supplied by Music Assistant. Review captures in .impeccable/review/shared use illustrative host metadata and a locally cached cover, not bundled sample music or artwork. Those captures establish layout only. Product and hardware-validation boundaries live in PRODUCT.md and docs/AFTERGLOW.md.

## Do's and Don'ts

- Do retain Archivo, warm paper, ink and square poster fields.
- Do derive track fields from artwork and check readable text contrast.
- Do preserve native controller focus and honest missing-data states.
- Do distinguish illustrative captures from hardware performance evidence.
- Don't reinstate mint focus, six top-level sections or the old small-artwork composition.
- Don't claim exact LG palette, animation or layout parity.
- Don't add blur, shadows or decorative chrome over MilkDrop.
