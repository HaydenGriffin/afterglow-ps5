# Changelog

Afterglow for PS5. Builds before 1.6.0 were private builds for one console; they are listed by milestone, with
the version where one was set. "Checked on hardware" means on one PS5 on firmware 7.61; anything else was
checked on a PC only (unit tests, mock servers and the off-screen preview).

# 1.6.0 — First public release (unreleased)

<!-- TODO(owner): correct this section after the lane merge; it lists what the round set out to build. If the
     46 texture presets ship, add them here and update the preset counts in README.md and docs/install.md. -->

- **Lyrics from Music Assistant, with an online fallback.** When Music Assistant has none, the PS5 looks the song
  up on [LRCLIB](https://lrclib.net) itself. **Online lyrics** (on by default, synced) turns that off; **Retry
  lyrics** asks again after a failed or empty lookup; **Lyrics timing** moves the words up to 10 s earlier or later.
- **Commands go to the right speaker.** Browse, the idle poster and the controls act on a command target: the
  chosen speaker, else the one followed, else the last one, else this PS5. Play now works with nothing playing.
- **Play on moves the music.** Picking an idle speaker under **Playing on** moves the music there, and **Play on
  this PS5** brings it back to the console.
- **Idle poster.** With nothing playing, the paused song or recent music is set as a poster, one press away from
  playing.
- Browse rows carry subtitles, and ticks mark what a row already is (a favourite, the speaker in use).
- The picture comes back when Music Assistant stops sending the waveform without ending the stream: the follow
  relay reconnects after a stall.
- **Log out** and token renewal revoke tokens by id, so they only ever remove this console's own token.
- The poster no longer shows a stream-quality badge (Details still has the format), and the song-change slide is
  gone, as on LG 1.5.0.
- The PS5 home screen shows the Afterglow poster block: a new icon, and selection and start-up backgrounds.
- Publishing: README, [install guide](docs/install.md), this changelog, issue and pull request templates,
  GitHub Actions for tests, lint and a release ZIP per version tag.

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
