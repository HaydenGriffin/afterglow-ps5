# Afterglow for PS5

An unofficial Music Assistant TV companion, built on BlackBearReloaded's
[ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui).
This is a foreground playback client, not a Music Assistant server fork.
Version 1.6.0 shares the LG Afterglow poster interface and core music
controls. Everything added since the parity build is host-tested only; see
[Not verified on hardware](#not-verified-on-hardware). Setting it up:
[install.md](install.md); history: [CHANGELOG.md](../CHANGELOG.md).

The PS5 connects directly to Music Assistant's Sendspin server on the LAN,
receives timestamped 48 kHz stereo PCM, and plays it through the console's
audio output. A separate graphics loop measures the actual output samples
to drive MilkDrop, halo and spectrum views. No Mac relay is needed at runtime.

## Playback and speaker following

Choose **This PS5** in Options → Settings → Source to play Music Assistant audio
through the console. Start music by selecting **Afterglow PS5** in Music
Assistant. MilkDrop, halo and spectrum respond to the output samples.

With a Music Assistant token configured, Source also offers **Active speaker**
and individual speakers. This mode draws the selected speaker's
music without playing a second copy through the PS5. Sync-group members resolve
to their active queue. Automatic following keeps a playing queue and otherwise
moves to the one that started most recently. A chosen speaker takes priority
while it plays and falls back to automatic following while silent.

Browse, the idle poster and the transport controls act on one **command
target** (as LG Afterglow 1.4.3 does): the chosen speaker, even while it is
silent (its own queue answers Play), else the queue followed, else the last one
followed, else this PS5 while it is the source, else the first speaker by name
(real speakers before groups). So Play now works with nothing playing, and
choosing an idle speaker in Playing on takes effect at once. With nothing
playing, the poster shows the target. On a group, or a sync leader with
members, volume sets the group's volume (`players/cmd/group_volume`).

Queue → **Playing on** lists This PS5, Active speaker, sync groups and
speakers. Picking a speaker with nothing of its own playing moves the target's
music there (`player_queues/transfer`, keeping its place; the row says "moves
the music here") and follows it; picking a speaker that plays just follows it.
Settings → Source only chooses and never moves music. When music elsewhere can
move, **Play on this PS5** (under Playing on in the Queue sheet, and as the
first row of Playing on) brings it to the PS5's own player, starts it there
even if it had stopped, and switches Source to This PS5. **Speakers together**
lists every speaker with a tick for those sounding the target's music; Cross
adds or removes one (`players/cmd/set_members` on the target). The leader,
fixed members and speakers it cannot group with are listed but refuse; the
section appears only when the target can lead a group.

### Idle poster

With nothing playing for 8 seconds, or 30 seconds after a pause, the screen
sets what was played as a poster: the paused song, or Music Assistant's
recently played music, with its cover, title, one field and a strip of covers
that always ends in **Browse**. Left/right walk the strip. Cross resumes the
paused song, plays a recent item on the command target (replacing its queue),
tries again when the list could not load, or opens Browse; the field names the
button and says so when a start has not taken after ten seconds. Every other
button keeps its usual meaning, and Options opens the controls over it. Only
the paused song wears its own colours; any other item sits on the neutral
fields. After three minutes without a button it dims to 35 %, and the first
button then only wakes it. It moves with OLED text movement, follows the night
register, rests MilkDrop while it shows, and with reduce motion the dimming is
immediate. It needs Music Assistant; without it the poster says how to set up.

The current build includes:

- Album artwork, track/artist/album information, a quality badge (for example
  "Lossless · FLAC 44.1 kHz / 16-bit") when Music Assistant supplies one,
  plain, timed or word-by-word lyrics, and the next two songs in the poster's
  Next field.
- Browse: Search (system keyboard), recently played, favourite
  playlists/albums/songs/artists, radio stations, all playlists and albums,
  podcasts and audiobooks; artist pages (albums, songs), podcast episodes;
  play now, next, add to queue, start radio and show songs. Long lists end in
  **More**, which loads the next page.
- Queue: **Playing on** (moving the music to a silent speaker, **Play on this
  PS5** and **Speakers together**), up next in a
  50-song window with **Earlier songs** / **Later songs** paging, play now,
  play next, move up/down and remove; shuffle, repeat, crossfade, volume,
  sleep timer and **Keep playing when the queue ends**.
- Visuals only, Visuals + track, Album and Lyrics display modes; a lyrics
  layer (off, one line, stage) over the visuals; plain lyrics stay explicitly
  untimed and are paged manually. Song changes slide a cover off each poster
  field, and the night register dims the poster from 22:00 to 06:00. With
  nothing playing, an idle poster offers the paused song or recent music.
- A preset browser, favourites shared with Music Assistant and LG viz-man,
  shuffle or sequential order, preset locking, a change timer, beat
  sensitivity (projectM's and the halo/spectrum beat detector's) and album
  colour that recolours each preset's waveform, border and motion-vector slots.
- Picture/text brightness, automatic visual level, 30/60/120 fps caps, idle
  frame holding, OLED text movement and reduce motion.
- **Sound & sync**: TV output compensation (this PS5's Music Assistant static
  delay) and picture/lyrics timing, drawn on a sync figure.
- **Account**: sign in on the console with the system keyboard, automatic
  token renewal, log out, and settings mirrored to the Music Assistant account.
- The DualSense light bar follows the artwork, optionally pulsing on the beat.
- A Home Assistant glance (the **Home** section) when configured.
- Saved preferences, a resumable preset performance scan, measured slow-preset
  filtering, manual keep overrides and interrupted-scan quarantine.
- Audio buffer, underrun, late-frame, reconnect, output-error and frame-rate
  diagnostics; automatic reconnection and bounded buffers; an opt-in dev
  request path for status, screenshots and a clean quit from a PC.

Browse pages hold 60 items (recent: 30; album/playlist songs: 200; search: 20
per kind) and **More** pages on to an offset of 2000. Home Assistant's ingress
URL is not the Sendspin or Music Assistant API endpoint.

**Background music while playing games is out of scope** (owner decision).
Afterglow is a foreground app; opening a normal homebrew title does not give
it Spotify's system music integration.

## Build locally

The toolchain runs in a local Linux container on either ARM64 or x86-64.
On Apple Silicon, it uses native ARM64 tools to cross-compile for the PS5;
no x86 emulation is needed. Use a local Docker runtime (for example Colima).
All compilation and preview rendering stays on the Mac. From this repository:

```sh
docker build -f Dockerfile.afterglow -t afterglow-builder .
docker run --rm -v "$PWD:/work" afterglow-builder make app
```

The result is `dist/PPSA99481/` and `dist/PPSA99481.zip`. It uses a separate
homebrew title identity; it does not update the LG webOS app. The original
reference gallery remains available to the host preview and unit tests, but
is excluded from the PS5 executable. Only the four required fonts and
Afterglow's icon and home-screen backgrounds are packaged; the upstream kit's
sound effects, music and artwork are not shipped.

Copy `afterglow.conf.example` to `dist/PPSA99481/afterglow.conf` and replace
the example address with the Music Assistant server's numeric LAN IPv4 address.
The default Sendspin port is 8927. Give each console a distinct `client_id`.
Afterglow supports only unencrypted LAN WebSockets; do not expose its port publicly.
Configuration is intentionally excluded from Git.

### Music Assistant token and metadata

Basic Sendspin playback does not need an API token. Artwork, lyrics, up-next,
shared favourites and speaker selection use Music Assistant's API on `ma_port`
(default 8095), on the same numeric IPv4 `host` as Sendspin.

The simplest route is Settings → Account → **Sign in**: the system keyboard
asks for the Music Assistant address (numeric IPv4, optionally `:port`;
empty uses `host`), user name and password. Afterglow creates a long-lived
token named for this console (one name per `client_id`), revokes the login's
session token, and stores the result in `/download0/hui/afterglow.auth`
(`/app0` is read-only). A signed-in token wins over `ma_token`. About eight
seconds after launch Afterglow checks the token's expiry and renews it when it
ends within 30 days, revoking the superseded one. **Log out** revokes every
token in this console's name and deletes the file. Sign-in, renewal and log out
swap the token without restarting the app.

Alternatively, create a separate long-lived token named **Afterglow PS5**
(`auth/token/create` with `{"name":"Afterglow PS5"}` from an authenticated
Music Assistant API client) and put it in `ma_token` in the private installed
`afterglow.conf`; keep the example file's value empty. Do not reuse or revoke
the working LG app's token. A file token is never renewed; replace it when it
expires and reopen the app.

With **Sync settings with Music Assistant** on (the default), the display,
visual, preset, timing and light-bar settings are mirrored to this device's
entry in the account's Afterglow preferences, and restored from it on a fresh
install. Off keeps them on this PS5.

Set `ma_player_id` to the PS5 player's Music Assistant `player_id`, obtainable
from `players/all`. This identifies its artwork, lyrics and queue while playing
on the PS5. It is Music Assistant's player ID, not necessarily the Sendspin
`client_id`. If omitted, Afterglow looks for an available player whose name
matches `name`; setting the ID explicitly avoids ambiguity between similarly
named players.

For visual-only speaker following, enable Music Assistant's **MilkDrop
Visualizer** plugin. Its `/milkdrop_visualizer` WebSocket relay runs on the
Music Assistant API port. Own-player metadata works without opening a relay
subscription. Artwork uses Music Assistant's HTTP image proxy; the native
fetcher accepts numeric IPv4 HTTP URLs, not HTTPS or hostnames.

Preferences and pending favourite changes are stored in
`/download0/hui/afterglow.prefs`. Favourite changes keep their timestamps until
Music Assistant acknowledges them, so a reconnect or app restart can retry
without overwriting newer changes from another device. Other LG device settings
and unrelated Music Assistant preferences are preserved.

Upload the app folder as [install.md](install.md) describes, wait for the
title to be registered and open Afterglow. On first connection, select the new
player in Music Assistant and play a track. Console sound, controller behavior
and firmware compatibility must be checked on hardware.

`dev_requests=<token>` (16 to 128 letters, digits, `-`, `_` or `.`) turns on
the development request path used by `tools/afterglow-dev.py`
(`status`, `screenshot <name>`, `quit`); it is off when the key is absent. See
"Dev requests" in [CONSOLE_VALIDATION.md](CONSOLE_VALIDATION.md).

When verifying uploads through [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv#features),
disable its default SELF-to-ELF conversion for that FTP connection with `SELF`.
Confirm the response says disabled before comparing executable hashes; converted
read-back bytes differ from the signed file even when the upload is intact.

### Home Assistant glance

The transport band's **Home** section shows a small Home Assistant glance:
headed groups of entities with their current values, updated live. Configure
it in the installed `afterglow.conf` (see `afterglow.conf.example`):

```ini
ha_host=192.0.2.20
ha_port=8123
ha_token=<long-lived access token>
ha_group=Living room:sensor.living_room_temperature=Temperature,light.living_room=Lights
ha_group=Outside:weather.home=Weather
```

- `ha_host` is a numeric IPv4 address, like `host`; names are not resolved.
- `ha_token` is a long-lived access token (at most 2048 characters). A
  non-admin user is enough; keep the file private.
- Each `ha_group` line is a heading, a colon and comma-separated entity ids,
  each optionally `=Label`. At most 8 groups of 12 entities; extra lines and
  invalid ids are ignored. Without a label the row uses the entity's friendly
  name with the group heading removed from its start.

Afterglow connects to `/api/websocket`, authenticates, and subscribes with
`subscribe_entities` limited to the configured entity ids, so it never
downloads the whole house's states. Numbers are rounded for the sofa
(temperatures to a tenth, percentages whole), weather and climate rows carry
their temperatures, and media players show what is playing.

Cross toggles lights, switches, fans and input_booleans (a drawn tick marks
"on") and runs scenes and scripts with `turn_on`. Nothing else is ever
called. A press shows its new state at once; if Home Assistant refuses it,
or the state has not followed within five seconds, the row returns to the
real state and a refusal is named under the list. The page says when it is
not set up, connecting, offline (with the retry delay; reconnects back off
from 1 to 30 seconds and keep the last known values on screen) or when the
token was refused (it is not retried until the app is reopened).

Host checks: `make test-unit GTEST_ARGS='--gtest_filter=AfterglowHome*'` and
`bash tests/integration/run-home-assistant-smoke.sh` (a disposable local
websocket fixture; it never contacts a real Home Assistant).

## Controls

On the poster or visuals, with no controls open:

| Control | Action |
| --- | --- |
| Cross | Play/pause on the command target; on the idle poster, resume or play the chosen item |
| L1/R1 | Previous/next track |
| D-pad left/right | Seek back/ahead 10 seconds; on the idle poster, move along the strip |
| D-pad up/down | Volume; pages plain lyrics in Lyrics mode |
| L2/R2 | Previous/next MilkDrop preset (while MilkDrop is shown); on the timed Lyrics poster, lyric timing −/+100 ms |
| Square | Cycle Visuals only, Visuals + track, Album and Lyrics |
| Triangle | Lyrics layer off/one line/stage over visuals; Album ↔ Lyrics on the poster |
| L3 | Lock/unlock the current preset |
| R3 | Add/remove the current preset from favourites |
| Options | Open/close the transport band |
| Hold Circle for 1.5 seconds | Close safely through the system |
| Touchpad | Nothing |

Options opens the transport band: a section strip at the top (Browse, Now
playing, Queue, Settings, and Home once Home Assistant is set up) and, at the
bottom, the playing song with eight tiles: Previous, Play/Pause, Next, Back
10 s, Ahead 10 s, Favourite, Details and Display. Left/right move along a row,
up/down switch between the strip and the tiles, Cross activates, L1/R1 step
through the sections (wrapping), and Circle or Options closes it; it also
closes after nine idle seconds. Details is the back of the poster (facts,
music favourite, start radio); Display holds the four modes, the lyrics layer,
**MilkDrop behind album and lyrics**, the track card, visual style (MilkDrop,
halo, spectrum), **Reduce motion** and **Word-by-word lyrics**.

Browse, Queue, Settings and Home open as sheets. Browse covers the screen;
the others sit beside the artwork slab. Up/down move through rows, left/right
adjust values (the focused row shows chevrons when it can be adjusted), Cross
selects or toggles, Circle goes back one level and then returns to the
transport band, and Options closes everything. Queue holds **Playing on**, up
next and the playback rows (shuffle, repeat, crossfade, volume, sleep timer,
keep playing when the queue ends). Settings contains Source, Visuals &
presets, Lyrics timing, Presets, Sound & sync, Account and Status. On Presets,
Cross loads the selected preset, R3 changes its preset favourite status, and
Square toggles **Keep**; in search results Square starts a new search. Music
favourites are separate from visual preset favourites.

Choose a lyrics layer with Triangle or in Display. **One line** replaces the
track card with the current timed line after the card's nine seconds;
**Stage** shows the current line large across the middle. Without timed lyrics
the track card remains and a notice says so. Lyrics mode shows
previous/current/next lines, manually paged plain lines, or an explicit
unavailable state. When the LRC carries enhanced word timings (`<mm:ss.xx>`
tags) and **Word-by-word lyrics** is on (the default), the current line lights
word by word; otherwise timing stays line-level and none is invented.
Settings → **Lyrics timing** shifts text by up to ±5000 ms in 50 ms steps;
positive values show lines later. LRC file offsets are honoured.

**Visual frame rate** offers 30, 60 or 120 fps. 120 requests a 120 Hz display
on the next app launch; it does not change PS5 system settings. The graphics
SDK falls back to 60 Hz if the output cannot accept 120, and Visuals & presets
and Status report the accepted refresh after the first presentation. Rendering
is capped to the lesser of that output refresh and the selected rate. An LG C1
supports 120 Hz, but cable/console output configuration and each preset's
actual performance must be checked on hardware. Sustained 120 fps is not yet
verified.

MilkDrop keeps running behind the Album and Lyrics posters by default (turn
off **MilkDrop behind album and lyrics** to give them a plain ground); the
transport band does not pause it, sheets do. **Reduce motion** freezes MilkDrop
on its current frame (it redraws only on a preset change), stops the OLED
drift and halo drift where they are, and skips beat lifts and the song-change
slide. **Hold visuals when
idle** freezes it while nothing plays.

Visuals & presets sets a 15–600 second preset duration; **Lock this preset**
stops automatic changes. An empty favourites list falls back to all eligible
presets. **Beat sensitivity** (0–200 %) sets projectM's beat sensitivity and
the beat detector behind the halo/spectrum lift and the light-bar pulse.
**Light bar follows artwork** (on by default) colours the DualSense light bar
from the artwork; **Pulse on the beat** (off by default) makes it pulse.

Settings → **Sound & sync** holds **TV output compensation**: this PS5's
Music Assistant static delay (0–5000 ms, 10 ms steps, 100 ms when held), saved
to Music Assistant 0.6 s after the last step. It applies only while the source
is This PS5 and needs Music Assistant. **Picture & lyrics timing** adjusts only
the follow-relay picture timing in 25 ms steps, up to ±2000 ms; positive values
show the picture later. A sync figure draws both against the music on a
±600 ms scale.

Start or resume the performance scan from Visuals & presets. It saves a checkpoint before
loading each preset, settles for 0.5 seconds, then measures five seconds using
a repeatable signal. Scanning is capped at 60 visual frames per second even
when the output runs at 120 Hz; the rating counts rendered visual frames. Allow roughly 35 minutes for the enabled set. Circle
pauses the scan; an interrupted load is quarantined on the next launch. The
minimum measured frame-rate threshold is 30, 40, 45 or 60 fps. These results
belong to this PS5; the LG benchmark is not used as PS5 performance evidence.

## Validation

```sh
make test-unit
make lint
AFTERGLOW_PREVIEW=1 tools/host-snapshots.sh build/afterglow-preview afterglow
# Night register, karaoke words, halo/spectrum held on a beat, album colour.
AFTERGLOW_PREVIEW=1 AFTERGLOW_NIGHT=1 tools/host-snapshots.sh build/afterglow-night afterglow
AFTERGLOW_PREVIEW=1 AFTERGLOW_LYRICS=karaoke tools/host-snapshots.sh build/afterglow-karaoke afterglow
AFTERGLOW_PREVIEW=1 AFTERGLOW_VISUAL=1 AFTERGLOW_BEAT=1 tools/host-snapshots.sh build/afterglow-halo afterglow
AFTERGLOW_PREVIEW=1 AFTERGLOW_TINT=100 tools/host-snapshots.sh build/afterglow-tint afterglow
tools/host-snapshots.sh build/afterglow-offline afterglow
CXX=clang++-18 bash tests/integration/run-sendspin-smoke.sh
CXX=clang++-18 bash tests/integration/run-companion-smoke.sh
CXX=clang++-18 bash tests/integration/run-companion-library-smoke.sh
CXX=clang++-18 bash tests/integration/run-companion-playback-smoke.sh
CXX=clang++-18 bash tests/integration/run-account-smoke.sh
CXX=clang++-18 bash tests/integration/run-home-assistant-smoke.sh
```

Host checks include real Music Assistant metadata/relay reads and disposable
mock servers that test masked WebSockets, decimal elapsed times, lyrics,
favourite merging, reconnect after a lost write acknowledgement, bounded
shutdown, browse/search paging, sign-in/renewal/log out and the Home Assistant
glance. The mocks never change real players, accounts or Home Assistant.

### Not verified on hardware

An earlier build passed hardware audio/title/artwork checks and measured
59.9–60 fps without audio errors. **Everything new in 1.5.0 and 1.6.0 is
unverified on the console**, including:

- the transport band, sheets and every controller binding with a DualSense
  in hand;
- search, artist pages, podcasts/audiobooks, More paging and the 50-song
  queue window;
- speaker group editing, TV output compensation and the sync figure;
- the command target, moving music between speakers (transfers, Play on this
  PS5), group volume and the idle poster (only the Python mocks have answered
  these writes);
- the system keyboard, console sign-in, token renewal, log out, the
  `afterglow.auth` write and settings mirroring;
- the light bar (colour and beat pulse), beat detection and projectM beat
  sensitivity;
- album slot recolouring, the night register's local time, the song-change
  slide, word-by-word lyrics and lyric timing on L2/R2;
- the Home Assistant glance, the dev request path and the quality badge;
- the 120 Hz option and sustained 120 fps.

Host checks and captures do not establish any of these.

Preview data is explicitly illustrative. A screenshot does not verify PS5 sound,
frame rate or timing. The network worker and audio output run independently of
the UI; no FFT or socket work occurs in the audio callback. Timestamp scheduling
currently uses a basic clock-offset estimate and a 3 ms tolerance, without
adaptive resampling or calibrated HDMI latency. Group synchronization and long
sessions need hardware measurement before they can be claimed reliable.

PS5 titles refuse `fcntl(F_SETFL, O_NONBLOCK)` and `ioctl(FIONBIO)` on sockets
with `EACCES`. The PS5 build switches sockets to nonblocking mode with the
console's own `SO_NBIO` option (`0x1200`, as in the upstream native-app
boilerplate) and logs its read-back; host builds keep `FIONBIO`.

While running, diagnostics are also written to
`/download0/hui/dev/app.log` inside the title sandbox. Through FTP this is
`/mnt/sandbox/PPSA99481_000/download0/hui/dev/app.log`. Record counters before and
after a listening session; a growing underrun or late-frame count is evidence
to investigate, not proof that a buffer-size increase alone will fix the issue.

Album colour rewrites each preset's waveform, outer/inner border and
motion-vector base colours in the preset text before projectM loads it, as LG
viz-man recolours those slots: the artwork hue at the preset's own brightness,
wide borders (over 0.05) keep their colour, blended by the 25-100 % strength.
LG fills the slots from Music Assistant palette roles; the PS5 has two artwork
colours, so the accent takes the waveform and inner border and the secondary
the outer border and motion vectors. Per-frame equations that assign those
variables still override them, as on LG. A colour change reloads the current
preset with a soft cut once the colours have settled for a second, at most
every three seconds.

The night register (22:00-06:00) reads local time on the console through the
kernel's UTC-to-local conversion; whether summer time is included has not been
checked on hardware. Host previews force it with `AFTERGLOW_NIGHT=1`.
Lyrics and artwork depend on provider availability. The queue shows a 50-song
window at a time. OLED text movement and idle frame holding
do not establish PS5 system-screensaver control. These differences remain when
comparing the native implementation with the LG app.

The project retains GPL-3.0-or-later licensing and upstream attribution.
See `THIRD_PARTY_NOTICES.md` for dependencies.

## MilkDrop build and preview

Both builds automatically compile the pinned projectM core using
`tools/build-projectm.sh`. A Linux Clang 18 toolchain, Ninja, Git and the
existing EGL/OpenGL dependencies are required. Upstream exceptions are enabled
inside projectM; the application keeps its existing exception-free build.
The PS5 adapter avoids `std::random_device` because its `arc4random` import
is unavailable to this native title. Host builds retain the upstream generator.

349 preset files are included, with 347 enabled in `assets/presets/index.tsv`.
Two Mandelbox presets render black with this projectM build and are disabled.
Original author names are retained in the index.
46 presets requiring external image textures are excluded. Licensing and source
provenance are in `THIRD_PARTY_NOTICES.md` and `third_party/presets/README.md`.
Notices are also copied into the application folder's `licenses` directory.

```sh
# One hard-cut capture per preset, in index order, using synthetic audio.
AFTERGLOW_MILKDROP=347 AFTERGLOW_MILKDROP_WIDTH=320 AFTERGLOW_MILKDROP_FRAMES=15 \
  tools/host-snapshots.sh build/milkdrop-all afterglow
# Default captures use the console's 1280 x 720 visualiser resolution.
AFTERGLOW_MILKDROP=3 tools/host-snapshots.sh build/milkdrop-preview afterglow
# Exercise next/previous presets and complete their 2.7-second transitions.
AFTERGLOW_MILKDROP=3 AFTERGLOW_MILKDROP_TRANSITIONS=1 \
  AFTERGLOW_MILKDROP_FRAMES=210 AFTERGLOW_MILKDROP_WIDTH=320 \
  tools/host-snapshots.sh build/milkdrop-transitions afterglow
```

The thumbnail sweep is a brief compatibility check. Default captures run 90
frames per preset; `AFTERGLOW_MILKDROP_FRAMES` can extend this up to 600.
`AFTERGLOW_PREVIEW=1` also composes a real MilkDrop texture behind the screen's
illustrative track metadata.

Captures advance projectM's animation clock at 60 Hz and report preset names,
visible pixel coverage and GL errors. Load, GL and image-write failures return
an error. They establish rendering compatibility, not console frame rate or
speaker synchronization.

## Two-platform release status

The LG Afterglow application is the design and feature reference. Both offer
own playback/following, browsing and search, queue controls, four display
modes, timed and word-by-word lyrics, sign-in and a sync figure. PS5 uses
projectM with 347 enabled presets; LG uses Butterchurn and its existing preset
set. PS5 still reads its server addresses from a private configuration file,
and has two artwork colours rather than LG's palette roles.

Treat both as unverified until controller/remote, audio, lyric timing and a
listening-session check pass on their respective hardware. LG
also has documented native graphics memory growth (about 3.4 MiB/min in the
existing constant-preset 30 fps measurement). Timing queries are already
disabled by default, but remaining growth is unresolved; do not claim
long-session stability from local build/tests.
