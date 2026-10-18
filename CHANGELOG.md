# Changelog

Afterglow for PS5. Builds before 1.6.0 were private builds for one console; they are listed by milestone, with
the version where one was set. "Checked on hardware" means on one PS5 on firmware 7.20; anything else was
checked on a PC only (unit tests, mock servers and the off-screen preview).

# Unreleased

Built and tested on a PC only.

- **Visual style Off.** Display's visual style gains **Off** after MilkDrop, Halo and Spectrum: no visuals, just the
  music on a black screen. With Visuals only nothing is drawn but the hints, which fade; with Visuals + track the
  track card sits on black; behind Album and Lyrics the posters stand on black. MilkDrop does not render while Off.

# 1.6.0 — First public release (16 October 2026)

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

Also new in 1.6.0, checked on a PC only so far (unit tests, mock servers and the off-screen preview):

- Music playing on another speaker can be joined or moved: Queue and Playing on offer **Join <speaker>** (this
  PS5's own player joins that speaker's group, where Music Assistant allows it) and **Move here** (the music
  moves to the PS5, as the old Play on this PS5 row did). Nothing happens until you choose one.
- Playing on says what each speaker plays: the song and artist, Paused, or Idle, instead of Speaker, Group or
  Grouped.
- Speakers together: **Volume for each speaker** sets one member's volume (`players/cmd/volume_set`) beside the
  group's Volume; speakers with a fixed volume are left out.
- **Cover art in the visuals** (Visuals & presets → Picture, on by default): the presets that sample a picture
  (worms, cells, seaweed, a sky…) get the song's cover instead of the generated clouds, faded in over 2 s once the
  artwork and its colours have arrived, at most every 4 s. One 512 × 512 texture owned by the app is updated in
  place, never one per song. Off, or no artwork, fades back to the clouds.
- **Change preset** (was Preset duration) goes on past Every 10 min to **Each song** (a new preset when the queue
  moves to another song, after 2 s, at least 10 s apart) and **On big moments** (when the music's energy jumps, at
  least 25 s apart). Both follow the preset list, favourites and skip-slow as the timer does; LG Afterglow decides
  the same way.
- **Scan to save.** Details shows a QR code beside the facts that opens the playing song on a phone, on a public
  service Music Assistant knows it from (Spotify, Tidal, Apple Music, YouTube Music, Deezer, Qobuz, SoundCloud),
  preferring the one it streams from. Drawn on the console with Project Nayuki's QR Code generator (MIT, C version);
  hidden when the song has no public page. The same link gives the same code as LG Afterglow.
- **Check my setup** (last row of Settings): Music Assistant set up, reachable and 2.10 or newer, the token
  accepted, this PS5 known as a speaker and Sendspin connected, the MilkDrop Visualizer plugin found (when following
  a speaker), the console's clock within 5 minutes of the server's, lrclib.net answering (busy is not a failure) and
  every preset measured, each failure with one next step. Runs only when asked. An idle poster with nothing to offer
  ends its strip in **Check setup**, and "Ready when you are." adds "Something not right? Check my setup in
  Settings." after 20 seconds without Music Assistant.
- **Keep a lyrics timing for one song.** Changing **Lyrics timing** during a song (in Settings → Lyrics, or with
  L2/R2 on the Lyrics poster) offers once, in Settings → Lyrics, **Keep this timing for this song?** (Keep / Not
  now). A kept timing applies to that song's lyrics only, the other songs keep theirs, and **Forget timing for this
  song** drops it. Kept for the 100 most recently changed songs, synced with this PS5's account settings (as LG's).
- **Choose other lyrics.** Settings → Lyrics → **Choose other lyrics** swaps wrong lyrics for another version from
  lrclib.net (up to six, with length, Timed or Plain and album; a much longer one says so). The pick is kept for the
  song and fetched by its LRCLIB id on later plays; **Automatic** goes back.
- **Go to a line.** Details → **Go to a line**: with timed lyrics, pick a line and the song plays from it.
- **Go to the album or artist.** Details offers Go to the album, Go to *artist* (up to three) and, for a podcast
  episode, Go to the podcast, when Music Assistant gives their ids; Browse opens on that page and Circle returns to
  Details.
- **Audiobooks and podcasts.** Browse has **Continue listening** (after Search; Browse still opens on Recently
  played). The idle poster offers up to three audiobooks or episodes in progress after the paused song ("Carry on").
  While one plays, Details offers **Speed** (0.75× to 2×) and **Chapters** (Cross plays from a chapter's start);
  Start radio and Retry lyrics are not offered for them. Empty Podcasts, Audiobooks and episode lists say so.
- A playing audiobook names its authors where a song names its artist.
- **Save the queue as a playlist.** Queue → **Save as a playlist** (under the Up next songs) saves the whole queue
  as a Music Assistant playlist named “Queue · 16 Oct” (unique with “(2)”), no keyboard needed. The row and a
  notice confirm with a tick, say how many songs could not be added, or why Music Assistant refused (an account
  that cannot change playlists, nowhere to keep them).
- **Add to a playlist** on Details, a queued song and a song in Browse: **New playlist** or one of the eight
  playlists you can change, most recently changed first; the song's own row confirms, as Play next does.
- **Tonight's programme.** After a listening session the idle poster offers **Tonight** (or **Today**) before
  Browse: the evening's songs set as a concert programme with their artists and start times, and **Keep it as a
  playlist**. Offered only, never opened by itself; built from Music Assistant's played history for the queue, so
  it survives a restart.
- **The Night logo in the app.** The *afterglow* wordmark, its o a sun setting behind the baseline, heads the
  screens you pass through: setting up and connecting ("Ready when you are." on the poster and on the track card)
  and Settings → Status. Never on the now-playing or idle posters, which can stay on an OLED for hours, nor while
  waiting connected for music. Read from `assets/brand/` once, on first use; a missing file draws nothing.
- **The posters look like LG's.** Album and Lyrics now show the complete cover in a square at the left margin under
  the place line ("This PS5"), on a dark ground with a touch of the artwork's colour, instead of a full-height crop
  and two coloured fields. Album sets the title, artists and album in one plain column; Lyrics puts the song and
  its artists under the cover and the line before, the line now and the line after in the column, centred, with
  the rule under the line now still filling as it is sung. Up next names the song and its artists above one thin
  progress line that runs the width of the screen between the times.
- The track card over the visuals and One line's Box are LG's dark ink box with paper words, at LG's sizes, instead
  of the artwork's colour. One line keeps the line after it and the rule that fills as it is sung.

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
