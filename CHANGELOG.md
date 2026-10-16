# Changelog

Afterglow for PS5. Builds before 1.6.0 were private builds for one console; they are listed by milestone, with
the version where one was set. "Checked on hardware" means on one PS5 on firmware 7.20; anything else was
checked on a PC only (unit tests, mock servers and the off-screen preview).

# 1.6.0 — First public release (unreleased)

Checked on hardware on 16 October 2026: launch, playback through the console at 59.9–60 fps, artwork, online
lyrics over HTTPS, Browse, the controls, the queue, Display settings and the lyric overlays, the Home section, dev
requests and the title's place under Media. Not yet checked on a console: sign-in and search with the system
keyboard, the light bar, 120 Hz, token renewal and timing drift over a long session.

- **New logo, shared with the LG app.** The wordmark's o is the sun setting below the baseline; the home-screen
  icon is a close-up of that sunset, and the selection and launch backgrounds show the whole word on the horizon.
- **Lyrics over visuals: Position, Background, Size.** Display adds three rows for the chosen overlay (One line or
  Stage), kept apart and synced: Top / Middle / Bottom, Box / Shade / None and Small / Medium / Large. Overlays stay
  clear of the notice line, the hints and the track card (One line at Bottom stacks above it).
- **Smooth progress.** The song progress bar and the lyric underline (line and word by word) move every frame from
  the interpolated position, easing small clock corrections and snapping only on seeks and new songs.
- **"More" on long lists.** Sheets and lists that run past their edge fade the edge row and say **More** with a
  chevron, top or bottom; the focused row always keeps a row of context.
- **Lyrics from Music Assistant, with an online fallback.** When Music Assistant has none (or errs, or takes more
  than 5 s), the PS5 searches [LRCLIB](https://lrclib.net) itself over HTTPS (BearSSL, certificates checked
  against the console's own CA list), sending only the song's title and first artist. Settings → **Lyrics** holds
  **Online lyrics** (on by default), **Retry lyrics** after a failed or empty lookup, and **Lyrics timing** up to
  10 s earlier or later in 100 ms steps with **Reset lyrics timing**.
- A positive LRC `[offset:]` now brings lyrics sooner, as the format defines.
- **Commands go to the right speaker.** Browse, the idle poster and the controls act on a command target: the
  chosen speaker, else the one followed, else the last one, else this PS5. Play now works with nothing playing.
- **Play on moves the music.** Picking a silent speaker under **Playing on** moves the music there, and **Play on
  this PS5** brings it to the console. **Speakers together** lists sync groups and explains refusals.
- **Idle poster.** With nothing playing, the paused song or recent music is set as a poster, one press away from
  playing; it dims after three quiet minutes.
- **Browse** rows read kind · artist · year (or length). Play now and Start radio return to Now playing; Play next
  and Add to the queue confirm on the item's own row with a tick.
- **Controls**: quick seek and held volume presses add up, skips collapse to one waiting skip per direction, a
  second Play/Pause before Music Assistant answers undoes the first, and synced speakers use group volume.
- The picture comes back when Music Assistant stops sending the waveform: the follow relay reconnects after 10 s
  without frames while the queue plays, without looping on a paused speaker.
- **Account**: token renewal hands the fresh token to every connection before the old one is revoked; **Log out**
  revokes only this console's own token, by id.
- **All 395 presets LG Afterglow ships**, 393 enabled. Presets that sample MilkDrop's texture images get a cloud
  texture the app generates (as Butterchurn does on LG); a projectM shader fix makes the two Mandelbox presets and
  "Martin - disco mix 6" render.
- The poster no longer shows a stream-quality badge (Details still has the format), the song-change slide is gone
  and "Next" reads "Up next", as on LG 1.5.0.
- With no server set up, the screen points at Settings → Account → Sign in.
- The PS5 home screen shows the Afterglow poster block: a new icon, and selection and start-up backgrounds.
- Afterglow is a media app: it sits under Media on the home screen, beside the other music and video apps, instead
  of under Games.
- Publishing: README, [install guide](docs/install.md), this changelog, issue and pull request templates,
  GitHub Actions for tests, lint and a release ZIP per version tag.
- Fixed on the console: muted text and rules (section headings, "Saved on this PS5", the time and status lines)
  were invisible on the PS5 while they drew on a PC; the poster colours are now compile-time constants.
- The song's artists read "Justice, Tame Impala, SZA" from Music Assistant's queue instead of Sendspin's
  slash-joined line.
- Fixed before release: lyrics and saved preset ratings longer than 2 KB were cut short (the cut lyrics also kept
  the online search from running); **Log out** now says when Music Assistant refused or did not confirm removing
  the token; a renewed or new token that cannot be saved no longer retires the one the next launch reads;
  unanswered account, settings and recent-music requests time out instead of blocking later ones; and HTTPS
  answers are capped on the wire, not only by their body.
- Dev requests can tap controller buttons (`tools/afterglow-dev.py press options right cross`), so a PC can drive
  the app for console screenshots. Off unless `afterglow.conf` sets `dev_requests`.

# 1.5.0 — Everything from the LG parity review (15 October 2026)

Built and tested on a PC; not installed on a console.

- **Browse**: search with the system keyboard, artist pages, podcasts and audiobooks, and **More** at the end of
  long lists.
- **Queue**: a 50-song window with **Earlier songs** / **Later songs**, move up and down, and speaker group editing
  under **Playing on**.
- **Sound & sync**: **TV output compensation** sets this PS5's Music Assistant static delay; a sync figure draws it
  against picture and lyrics timing.
- **Account**: sign in on the console with the system keyboard (the PS5 keeps a long-lived token named after the
  console, never the password), automatic token renewal, log out, and settings mirrored to the Music Assistant
  account.
- Word-by-word lyrics when the LRC carries word timings; lyric timing on L2/R2 in the Lyrics view.
- The DualSense light bar follows the artwork, optionally pulsing on the beat; **Beat sensitivity** drives projectM
  and the halo/spectrum beat detector.
- Album colour recolours each preset's waveform, border and motion-vector colours.
- A night register dims the poster from 22:00 to 06:00.
- A **Home** section: a small Home Assistant glance with live values, configured in `afterglow.conf`.
- An opt-in dev request path (status, screenshot, clean quit) for checking a console session from a PC.

# 1.4.0 — The LG poster interface (14 October 2026)

- The interface now follows LG Afterglow's concert poster: Archivo, a full-height artwork slab, large title and
  lyrics, flat artwork-colour fields and inverse focus.
- Four saved display modes: Visuals only, Visuals + track, Album and Lyrics. A lyrics layer (one line or stage)
  over the visuals; plain lyrics are paged by hand and never given invented timing.
- **Visual frame rate** of 30, 60 or 120 fps; 120 asks for a 120 Hz display on the next launch and falls back to
  60 Hz when the output refuses.
- A UI pass the same night: settings became a full-height sheet beside the artwork with a now-playing header, the
  Queue lists only upcoming songs, and the footer shows DualSense button glyphs. This build was installed on the
  console; it has not had a full hardware check.

# Parity build — Music Assistant companion (14 October 2026)

Checked on hardware: audio, title and artwork with the music playing; 59.9–60 fps over a listening session with no
underruns, late frames, reconnects or output errors.

- With a Music Assistant token: artwork, lyrics, up next, favourites shared with Music Assistant and LG, and
  following another speaker's music through the MilkDrop Visualizer relay without sounding a second copy.
- An Options panel for playback, display and presets; a resumable preset performance scan that measures each preset
  on this PS5, filters slow ones, and quarantines a preset that stopped a scan.

# Phase 1 — Sendspin player with MilkDrop (14 October 2026)

Checked on hardware: Afterglow joined Music Assistant as a speaker and played, with presets rendering and switching
at 59.9–60 fps and no underruns.

- The PS5 connects to Music Assistant's Sendspin server, plays the timestamped 48 kHz stereo stream through the
  console's audio output, and draws MilkDrop (projectM) from the samples it plays.
- 349 presets from butterchurn-presets, 347 of them enabled; halo and spectrum views.
- PS5 sockets refuse the usual non-blocking calls; Afterglow uses the console's own `SO_NBIO` option.

# Start

Afterglow for PS5 began as a fork of BlackBearReloaded's
[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) at `bbe1905`, keeping its kit, build and
packaging.
