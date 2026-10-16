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
    fontSize: 97px
    lineHeight: 1.08
  display:
    fontFamily: Archivo
    fontSize: 82px
    lineHeight: 1.08
  display-long:
    fontFamily: Archivo
    fontSize: 69px
    lineHeight: 1.08
  display-xlong:
    fontFamily: Archivo
    fontSize: 56px
    lineHeight: 1.08
  lyric-now:
    fontFamily: Archivo
    fontSize: 72px
    lineHeight: 1.08
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
  poster-cover:
    width: 576px
    height: 576px
  poster-title:
    textColor: "{colors.paper}"
    typography: "{typography.display}"
    width: 998px
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
    backgroundColor: "rgba(18,20,23,0.9)"
    textColor: "{colors.paper}"
    typography: "{typography.caption-title}"
    rounded: "{rounded.none}"
    width: 1190px
  spectrum-bar:
    rounded: "{rounded.spectrum-bar}"
    width: 16px
---

# Design System: Afterglow PS5

## Overview

**Creative North Star: "The Tonhalle Poster"**

Afterglow PS5 adapts the current LG webOS Afterglow: the complete cover in a square, a heavy Archivo title, warm paper on a dark ground touched by the artwork's colour. The music owns the screen; controls stay readable from the sofa and MilkDrop retains its own visual language.

This records the implemented native adaptation, not exact LG CSS parity. The authority is the LG DESIGN.md and the native source; the retained upstream gallery is outside this surface. The previous Inter/Montserrat chrome and six-section information panel are superseded.

**Key Characteristics:**
- Archivo poster typography and an inset square cover at the left margin, as LG's poster.
- One plain column of words on the dark ground, warm paper focus inversion and no fixed chrome accent.
- Four display modes; Options opens a transport band (section strip and eight tiles) that leads to Browse, Queue, Settings and Home sheets.
- A single ink caption box over the visualizer, optionally with a timed lyric above it.
- The Night wordmark only on screens you pass through: setup, connecting and Settings → Status.

## Colors

Ink and Paper are the fixed chrome neutrals; muted paper carries secondary text and Rule carries hairlines. Panel focus fills with Paper and reverses text to Ink. The internal panel constant named kMint now resolves to Paper.

Field A comes from the native artwork colour, with readable_field choosing ink or paper at a target contrast of 4.5:1 and darkening in 15% steps when needed. Field B comes from the secondary artwork colour (the accent × 0.45), contrast checked the same way; if it sits within 1.25:1 of A it falls back to A × 0.55. These are runtime colours, not fixed brand accents or LG's full palette-role extraction. Before artwork decodes the fields are neutral greys (#3e4248 and #26292e), never a white slab on an OLED. The now-playing posters carry no fields: their ground is ink mixed with 14 % of Field A, and Field A backs the cover while it loads. The idle poster keeps its fields.

Amber and Mint remain restricted to the halo/Spectrum visualization and reference identity metadata. They are not active navigation colours. MilkDrop keeps preset colours unless album colour is enabled; then the preset's waveform, border and motion-vector slots take the artwork hue at the preset's own brightness (LG viz-man's slot recolouring), blended by the 25-100 % strength. Halo and Spectrum lift slightly on detected beats, never under reduced motion.

**The No House Accent Rule.** Chrome emphasis uses Paper; track fields take their colour from artwork.

## Typography

The native entry point loads archivo-regular, archivo-semibold and archivo-display baked font atlases; generic FontRef comments mentioning Inter/Montserrat are stale toolkit labels. Archivo carries the current interface. The bundled mono font is not the poster voice. Native font instances replace LG's browser variable-font stack; no CSS fallback or live variation axis is implemented.

The Album title uses LG's length ladder (.poster-title data-length): 97 at string length ≤12, 82 at ≤26, 69 at ≤50, otherwise 56 logical pixels, Archivo Display, up to three lines in 998 pixels at 1.08 line spacing, first baseline 238 + 0.86 × size. Native string length is the source's byte-count test. The idle poster keeps its own ladder (184/144/104/80 at 0.94, last baseline 608). Secondary metadata is fitted to its available width.

Timed Lyrics follows LG's stack: the line now in Archivo Display at 72 pixels (78-pixel lines, up to four), the lines before and after in Regular at 37 (48-pixel lines, up to two) in muted paper, 40 pixels between them. Plain Lyrics uses 44-pixel semibold text on 52-pixel lines, each entry up to two lines, paged by measured height (360 pixels per page). Caption titles use 58 pixels, or 64 for singalong; field titles use 52. Navigation is 30, body metadata 28, supporting information 26 and hints 24. Text uses native baseline positioning and default zero tracking.

## Layout

Fixed 1920 × 1080 logical canvas; MilkDrop renders at 1280 × 720 and fills it. No mobile breakpoints are implemented. Album and Lyrics follow LG's player.css (1vh = 10.8 px, 1vw = 19.2 px): the place name ("This PS5", the group, or "Listening room · Preview") at x=96 and the status right-aligned at 1824, both 26 px muted on baseline 92, no rule; the complete cover, unstretched square artwork, at (96,173,576,576) (LG 16vh, 30vw); the word column from x=826 (43vw) to 1824. Album sets the title from baseline 238 + 0.86 × size, the artists at 38 px (two lines from baseline 651, 48 apart) and album · year at 28 px muted (LG .poster-meta at 57vh). Lyrics sets the song (37 px semibold, 30 past 50 bytes, two lines from baseline 818) and its artists (28 px muted, two lines) under the cover, and centres the lyric stack in the column between y=194 and 820 (LG 18vh, 58vh); untimed lyrics page from baseline 300 with their page note on 740. Up next sits on baseline 900 in the column: "Up next" muted, the song in semibold paper, its artists muted, all 26 px. One 2-pixel progress line runs the width at y=958 between the elapsed and total times (26 px muted, baseline 967, each end as wide as the total); LG's sits at 91vh, here it stays above the controller hints at y=1000. Sheets keep the poster's left side: ground, place line and cover, cut by the sheet at x=656.

Visuals + track places LG's #track at x=96 with bottom y=980: an ink box at 90 % fitted to its words up to 1190 pixels (62vw), padded 22 / 31 / 24, the title in Archivo Display 58 (66-pixel lines, two at most), the artists in semibold 32 and album · year · label in 26. On setup and connecting a 360-pixel wordmark heads it. Visuals only hides this field. Display hints are a right-aligned glyph row (30-pixel glyphs, 24-pixel labels, centre y=1000, ending at x=1814), on an ink plate over the visuals; plain Lyrics adds D-pad Lyrics.

Options opens the transport band rather than a sheet. A 108-pixel ink strip slides down from the top with the sections (Browse, Now playing, Queue, Settings, and Home once configured), the current one ticked and the focused one filled with paper; L1/R1 Sections and Circle Close hints sit at its right. An ink band slides up to y=810 with the playing title · artist (26 px) and elapsed / duration, then eight 192 × 119 tiles 24 pixels apart from x=96: Previous, Play/Pause, Next, Back 10 s, Ahead 10 s, Favourite, Details, Display. Each tile is a 40-pixel drawn icon over a 26-pixel label under a two-pixel rule; focus fills it with paper. The band does not cover or pause the visuals.

Sheets (Queue, Settings and its pages, Details, Display, Home) are a full-height ink panel from x=656, beside the poster's artwork slab, with text from x=680 to 1824: a 65-pixel title on baseline 118, an optional note on 160, a rule at 164 (188 with a note), 76-pixel rows and 64-pixel headers down to y=936, a notice line on 966 and a DualSense glyph footer (34-pixel glyphs, centre y=1000) that changes with the page and focused row. Browse, search, artist and collection pages cover the screen: a 97-pixel title on baseline 142, a category column (63-pixel rows) from x=96 to 515 and the collection from x=539 (104-pixel rows; 86 for item actions). Sound & sync adds a sync figure placing "Picture & lyrics" against "The music" on a ±600 ms scale. Open sheets pause MilkDrop; Album and Lyrics do not unless "MilkDrop behind album and lyrics" is off.

## Elevation & Depth

Flat fills and stacking order create depth, without blur or shadows. The poster's ground is ink touched by the artwork (translucent over MilkDrop when it plays behind); the controls are opaque ink. Picture brightness darkens the visual layer; text brightness currently adds an ink overlay over the composed poster/caption before the controls panel is drawn. From 22:00 to 06:00 local time the night register (on by default) steps paper down to #c4c0b9, secondary text to 72 % of it, the artwork to 72 % opacity and the fields to 72 % of their colour, as LG's player.css does.

Chrome opacity approaches its target by 1 − exp(−12 × dt), with drawing stopped below 0.01. The At track changes card hides the caption after nine seconds. OLED movement shifts the poster/caption by a sinusoidal six pixels; reduced motion freezes MilkDrop on its current frame (redrawn only on a preset change), stops the drift phase and halo drift, and skips beat lifts.

## Shapes

Poster fields, controls, focused rows and hints have square corners. Hairlines are generally two pixels; active navigation has a three-pixel underline. The visualizer is separate: Spectrum retains rounded bars, halo retains three elliptical outlines, and the performance-scan notice retains its rounded native shape. These exceptions are not a rounded-card chrome vocabulary.

## Components

**Navigation and lists.** Options opens the transport band; L1/R1 step through Browse, Now playing (the band itself), Queue, Settings and Home, wrapping. The Details tile opens the back of the poster (facts, music favourite, start radio); the Display tile opens the display sheet (modes and lyrics layer with ticks, MilkDrop behind posters, track card, visual style, reduce motion, word-by-word lyrics). Queue holds Playing on, the 50-song up-next window with Earlier/Later songs, and the playback rows (shuffle, repeat, crossfade, volume, sleep timer, autoplay). Settings nests Source, Visuals & presets (grouped Picture / Presets / Performance / Controller light), Lyrics timing, Presets, Sound & sync, Account and Status. Every row names its action, so reordering a page never changes behaviour. A list that runs past its edge fades the edge row into the ink (70 % ramp, then solid) and sets a 26-pixel semibold "More" with a stroked chevron on it, right-aligned with the values; the focus keeps a row of context so the cue never covers it. Adjustable rows show stroked chevrons around the value only while focused (the Archivo faces carry no single angle quotes); rows that open a page carry a trailing chevron. Left/right only changes adjustable values; toggles and actions take Cross. Row focus is a full paper fill with ink text, with no breathing or horizontal shift. On the poster and over visuals, controller hints are glyphs that fade six seconds after the last input or track change, and L2/R2 Preset appears only while MilkDrop is shown.

**Poster.** As LG's: the cover in its square, the words in one plain column on the dark ground, no coloured fields. Album gives the column the title, the artists and the album; Lyrics moves the song and its artists under the cover and gives the column real timed lines (the line before, the line now, the line after), manually paged untimed text or an explicit unavailable message. The PS5 keeps two things LG does not: Up next names the artists as well as the song, and a 4-pixel rule under the line now fills as it is sung (paper over the hairline). The status line says Playing or Paused and the lyrics' state; format and quality live in Details. Artwork only draws when a Music Assistant texture exists; Field A remains as its backing.

**Brand.** The Night wordmark (assets/brand/afterglow-wordmark-paper.png: paper letters, the o an amber #EEB875 sun setting below the baseline) appears only where you pass through or look on purpose: 400 pixels wide over "Ready when you are." while setting up or connecting (the poster's waiting screen, baseline 330 at x=96), 360 at the head of the track card in the same states, and 200 at the top right of Settings → Status beside the title. Never on the now-playing or idle posters, nor on the connected waiting screen, which can stay for hours: a still logo marks an OLED. The picture is read and uploaded once, on first draw; without it nothing is drawn.

**Idle poster.** With nothing playing for 8 s (30 s after a pause) a full-screen poster replaces the display mode, as LG's #idle: target name and Paused / Nothing playing on baseline 92 over the rule at 124, a 544-pixel cover box at (96,173), the title on the length ladder with its last baseline at 608, Field A at (680,670,706,248) holding the item's detail (40 px semibold) and a Cross glyph hint, and a strip of 88-pixel covers from x=680 at y=936 that ends in a Browse word tile; focus fills a tile with paper. Only the paused song wears its colours; recent items, Browse and Try again sit on the neutral fields. After three quiet minutes an ink veil dims everything to 35 % (eased, immediate under reduced motion); the night register and OLED movement apply.

**Tonight's programme.** A page of the idle poster, opened only from its **Tonight** / **Today** word tile (just before Browse; the strip then keeps seven recent covers). Ink ground, the poster's top line (queue name left, "14 songs · 19:40 to 22:15" right, 26 px muted) over the hairline at 124. One hero: the word in columns 1–4 at 144 px display on baseline 262 (shrunk only if it would leave the columns) over the evening's date at 56 px display. The songs run in two ruled 560-pixel columns from x=680, eleven 70-pixel rows each from y=148: a muted number, the title (28 px semibold) over the artist (26 px muted), the start time right in muted figures; past 22 songs the last row reads "and N more". **Keep it as a playlist** and **Back** are ruled 80-pixel rows at the foot of columns 1–4, ending with the list at 918; focus fills paper with ink text, a success wears the drawn tick and its words sit under the label. Cross/Circle glyph hints at 976. No artwork and no field colour: a programme is typographic. The poster's dim veil and OLED movement apply.

**Visual caption and singalong.** Visuals + track is the saved default. A single ink box carries the title and artist/album. With singalong enabled and a current timed line available, that line replaces the large title and title/artist stays below. Untimed lyrics never receive invented timestamps. One line and Stage each take a Position (Top below the toast line at y=216, Middle centred on 540, Bottom on 980 for One line and 970 for Stage, stacking 16 px above the track card), a Background (Box: for One line LG's 90 % ink box, 27 × 15 padding, the line now in semibold 44 and the line after in 30 muted; for Stage the 82 % ink band; Shade: a full-width ink band 2.2x the text block, vertical gradients only, 78 % at the words; None: words with a soft two-ring ink outline, quieter words greyed rather than see-through) and a Size (0.8x, 1x, 1.25x of every measure). With karaoke on and enhanced LRC word timings, the current line lights word by word (unsung words at 42 % of its opacity) on the Lyrics poster, the one-line field and the stage. Visuals only can still briefly show help and the preset name.

**Presets and settings.** Presets support loading, R3 favourite and Square Keep; L2/R2 selects presets outside the panel (lyric timing on the timed Lyrics poster) and L3 locks automatic changes. Visuals & presets exposes brightness, album colour and strength, night register, duration, order, favourites, beat sensitivity, frame caps, performance scanning and the light bar. A 120 fps choice requests 120 Hz on the next launch, falls back to 60 when unsupported, and reports accepted refresh; it is not a hardware performance claim.

**State and provenance.** Missing lyrics, empty queues, disconnected sources and unavailable actions use explicit text. Metadata is supplied by Music Assistant. Review captures in .impeccable/review/shared use illustrative host metadata and a locally cached cover, not bundled sample music or artwork. Those captures establish layout only. Product and hardware-validation boundaries live in PRODUCT.md and docs/AFTERGLOW.md.

## Do's and Don'ts

- Do retain Archivo, warm paper, ink and LG's poster: the complete square cover and one column of words.
- Do derive the ground tint, the idle fields and the stage accent from artwork and check readable text contrast.
- Do preserve native controller focus and honest missing-data states.
- Do distinguish illustrative captures from hardware performance evidence.
- Don't reinstate mint focus, six top-level sections, the full-height cropped slab or coloured fields on the now-playing poster.
- Don't put the wordmark on a poster or any screen that can stay for hours.
- Don't claim exact LG palette, animation or layout parity.
- Don't add blur, shadows or decorative chrome over MilkDrop.
