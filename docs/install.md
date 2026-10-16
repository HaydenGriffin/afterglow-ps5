# Setting up Afterglow for PS5

A walkthrough from nothing to a PS5 that plays your Music Assistant music and draws it. Budget an hour the first
time: most of it is the preset scan running on its own.

1. [Check what you need](#1-what-you-need)
2. [Prepare Music Assistant](#2-prepare-music-assistant)
3. [Get the app](#3-get-the-app)
4. [Copy it to the console](#4-copy-it-to-the-console)
5. [Start it](#5-start-it)
6. [Sign in](#6-sign-in)
7. [Play something](#7-play-something)
8. [Follow a speaker instead](#8-follow-a-speaker-instead)
9. [Fix the timing](#9-fix-the-timing)
10. [Run the preset scan](#10-run-the-preset-scan)
11. [The configuration file](#11-the-configuration-file)
12. [Home Assistant glance](#12-home-assistant-glance)
13. [Keeping it running](#13-keeping-it-running)
14. [Lyrics and privacy](#14-lyrics-and-privacy)
15. [When something goes wrong](#15-when-something-goes-wrong)

---

## 1. What you need

- **A PS5 that runs native homebrew.** Afterglow is a native title in a folder: the console must be jailbroken, with
  a homebrew enabler that registers and starts folder titles from `/data/homebrew/<TITLE_ID>`.
  [ShadowMount Plus](https://github.com/drakmor/ShadowMountPlus) is what the build and its tooling were developed
  against: it registers a folder about ten seconds after it stops changing. You also need an FTP server on the
  console (port 2121 is the usual one) to copy the folder across.

  Afterglow has been run on **one console, on system software 7.61**. The packaging it inherits from
  ps5-native-app-boilerplate was checked on 6.02 and 12.70, but Afterglow itself has not been tried on any other
  firmware or enabler.
  <!-- TODO(owner): name the jailbreak and homebrew enabler (and versions) used on the 7.61 console, and link
       their own setup guides. Afterglow's docs should not explain how to jailbreak a console. -->

- **Music Assistant 2.10 or newer** on the same network, reachable by its **numeric IPv4 address** (for example
  `192.168.1.10`). The PS5 does not resolve names such as `music-assistant.local`, and Home Assistant's ingress
  address is not Music Assistant's: Afterglow needs Music Assistant's own address. It uses plain, unencrypted
  connections on your LAN:

  | Port | What for |
  | --- | --- |
  | 8927 | Sendspin, the audio stream to the PS5 |
  | 8095 | Music Assistant's API: sign-in, artwork, lyrics, the queue, browsing, the MilkDrop Visualizer relay |

- **A Music Assistant user account**, the one you log into its web interface with.
- **To follow other speakers** (draw what they play without sounding on the PS5): Music Assistant's **MilkDrop
  Visualizer** plugin, and speakers Music Assistant streams to itself (Sonos, Chromecast, AirPlay, Squeezelite,
  Snapcast, DLNA and the like). A speaker playing on its own, such as Spotify straight from the Spotify app, carries
  nothing to draw.

## 2. Prepare Music Assistant

- **Sendspin.** The PS5 plays as a Sendspin client of Music Assistant's Sendspin server on port 8927. When it
  connects, a player named *Afterglow PS5* appears in Music Assistant.
  <!-- TODO(owner): say whether a fresh Music Assistant 2.10 needs the Sendspin provider added by hand
       (Settings → Providers) or has it on by default. -->
- **MilkDrop Visualizer plugin** (only for following speakers): Music Assistant's web interface → **Settings →
  Plugins → Add plugin → MilkDrop Visualizer**. Keep its defaults and save. It streams the waveform of whatever is
  playing, which is what the PS5 draws when it follows a speaker. Playing on the PS5 itself does not need it.

## 3. Get the app

Download `PPSA99481.zip` from the [releases page](https://github.com/HaydenGriffin/afterglow-ps5/releases). The
release also has a `SHA256SUMS` file, and each ZIP built by GitHub Actions carries a signed provenance attestation:
`gh attestation verify PPSA99481.zip -R HaydenGriffin/afterglow-ps5`.

Or build it (any Linux or macOS machine with Docker, no PS5 tools needed):

```sh
docker build -f Dockerfile.afterglow -t afterglow-builder .
docker run --rm -v "$PWD:/work" afterglow-builder make app
```

The result is `dist/PPSA99481/` and `dist/PPSA99481.zip`. The package is the same for every console and contains no
addresses or credentials.

## 4. Copy it to the console

Extract the ZIP on your computer. It holds one folder, `PPSA99481`. Copy that **whole folder** into
`/data/homebrew/` on the console with an FTP client, so that you end up with:

```
/data/homebrew/PPSA99481/eboot.bin
/data/homebrew/PPSA99481/sce_sys/param.json
/data/homebrew/PPSA99481/sce_module/libc.prx
/data/homebrew/PPSA99481/assets/...
/data/homebrew/PPSA99481/licenses/...
```

- Don't upload the ZIP itself, and don't spread its contents directly into `/data/homebrew`.
- Every file must keep its permissions open to all (the ZIP stores them as 0777); the console refuses to start a
  title whose files are not.
- From a checkout, `make deploy PS5_HOST=<console address>` builds and uploads the folder for you, publishing
  `eboot.bin` and `param.json` last; see [Deployment](platform/DEPLOYMENT.md).

## 5. Start it

Wait until the enabler has registered the title (with ShadowMount Plus, about ten seconds after the upload finishes;
it says so). **Afterglow** then appears on the home screen with the AFTER/GLOW tile. Starting it earlier shows the
system's "title not found" screen: nothing is harmed, wait and try again.

The home-screen icon and backgrounds are copied once, when the title is registered. Hold **Circle** for 1.5 seconds
to close Afterglow; it saves its settings and asks the system to close it.

## 6. Sign in

Basic playback works without signing in once Afterglow knows Music Assistant's address. Everything else (artwork,
lyrics, up next, browsing, the queue, favourites, following speakers) needs a Music Assistant token, and signing in
on the console is the simplest way to get one.

Press **Options**, then go to **Settings → Account → Sign in**. The system keyboard asks for three things in turn:

| Field | Enter |
| --- | --- |
| Address | Music Assistant's numeric IPv4 address, with `:port` if its API is not on 8095. Example: `192.168.1.10` |
| Username | Your Music Assistant username |
| Password | Its password |

What happens: Afterglow logs in once, asks Music Assistant for a **long-lived token named after this console**, revokes
the login's short session token, and keeps the long-lived one in the title's own storage
(`/download0/hui/afterglow.auth`). The password is not stored. The token appears in Music Assistant under your user's
tokens; you can remove it there at any time, or with **Log out**. About eight seconds after each start Afterglow
checks the token and renews it when it ends within 30 days.

Without an address in `afterglow.conf`, the PS5 also takes the Sendspin audio stream from the server you signed in
to (on port 8927).

With **Sync settings with Music Assistant** on (the default), this console's display, visual, preset, timing and
light-bar settings are mirrored to your Music Assistant account and restored from it on a fresh install.

## 7. Play something

In Music Assistant, pick **Afterglow PS5** as the player and play a song, or use Afterglow's own **Browse** (Options →
Browse) to search and play. Within a few seconds the PS5 plays through the TV or your HDMI sound system and draws
MilkDrop from what it plays. **Square** cycles Visuals only, Visuals + track, Album and Lyrics; **Triangle** puts
lyrics over the visuals.

Starting Afterglow does not take over music that is playing elsewhere: it plays when Music Assistant sends it
something.

## 8. Follow a speaker instead

To draw what another speaker plays, without a second copy of the sound from the PS5: **Settings → Source** and pick
**Active speaker** or one speaker by name. *Active speaker* draws whatever is playing (the most recently started
speaker when several are); a chosen speaker is drawn whenever it plays, and *Active speaker* fills in while it is
silent. Speakers in a sync group all draw the group's music. This needs the MilkDrop Visualizer plugin (section 2).
**This PS5** goes back to playing on the console.

## 9. Fix the timing

- **TV output compensation** (Settings → Sound & sync): your TV or sound system adds a delay the PS5 cannot see.
  This sets this PS5's *static delay* in Music Assistant, 10 ms a step (100 ms when held, up to 5000 ms), so the PS5
  plays in time with your other speakers. It is saved to Music Assistant a moment after the last press, and applies
  while the source is **This PS5**.
- **Picture & lyrics timing** (same page): when following a speaker, moves the picture and lyrics against the sound,
  25 ms a step, up to 2 s either way. Positive values show them later. A bass-heavy song with a plain preset makes
  the offset easy to see.
- **Lyrics timing** (Settings → Lyrics timing): moves only the words, when a song's lyrics run early or late.

The sync figure on the Sound & sync page draws the two against the music.

## 10. Run the preset scan

The scan measures every preset on your console and keeps a list of the ones that run smoothly.

1. **Settings → Visuals & presets → Measure preset performance.** Each preset loads, settles for half a second, then
   runs for five seconds on a repeatable test signal.
2. Leave it for about **35 minutes**. **Circle** pauses; the same row picks up where it stopped. A preset that stops
   the scan is set aside on the next start.
3. Afterwards the preset list skips presets under the minimum frame rate you choose (30, 40, 45 or 60 fps). Under
   **Presets**, **Square** keeps a preset the scan would drop.

The scan belongs to this console; an LG TV's results say nothing about it.

## 11. The configuration file

Most setups need no file at all. `afterglow.conf` is a plain text file you put in the install folder,
`/data/homebrew/PPSA99481/afterglow.conf`, for what the console's own settings don't cover. Start from
[`afterglow.conf.example`](../afterglow.conf.example):

| Key | Default | Use |
| --- | --- | --- |
| `host` | none | Music Assistant's numeric IPv4 address, if you'd rather not sign in, or want the audio from a fixed server |
| `port` | `8927` | Sendspin port |
| `ma_port` | `8095` | Music Assistant API port |
| `client_id` | `afterglow-ps5` | The Sendspin client id. **Give each console its own** if you have more than one |
| `name` | `Afterglow PS5` | The player's name in Music Assistant |
| `ma_token` | none | A Music Assistant long-lived token instead of signing in. A token from signing in wins over it. It is never renewed; replace it when it expires |
| `ma_player_id` | none | This console's Music Assistant player id, if several players share its name |
| `ha_host`, `ha_port`, `ha_token`, `ha_group` | none | The Home Assistant glance (section 12) |

The file sits in the install folder, which the console's FTP server can read; keep tokens out of it if others can
reach that. Edit it with Afterglow closed, and reopen Afterglow afterwards.

## 12. Home Assistant glance

The **Home** section shows a few Home Assistant entities with live values, and lets you toggle lights, switches, fans
and input_booleans and run scenes and scripts. Nothing else is ever called. Configure it in `afterglow.conf`:

```ini
ha_host=192.0.2.20
ha_port=8123
ha_token=<long-lived access token>
ha_group=Living room:sensor.living_room_temperature=Temperature,light.living_room=Lights
ha_group=Outside:weather.home=Weather
```

- `ha_host` is a numeric IPv4 address, like `host`.
- `ha_token` is a long-lived access token from your Home Assistant profile (Security). A non-admin user is enough.
- Each `ha_group` line is a heading, a colon, and comma-separated entity ids, each optionally `=Label`. At most 8
  groups of 12 entities.

Afterglow subscribes only to the entities you list, never to the whole house.

## 13. Keeping it running

- **Updating**: close Afterglow, then copy the new `PPSA99481` folder over the old one (or `make deploy`). Keep your
  `afterglow.conf`. Settings and the sign-in live in the title's own storage, not in the folder.
  <!-- TODO(owner): confirm on the console that preferences and afterglow.auth survive replacing the folder. -->
- **Signing out**: Settings → Account → **Log out** revokes this console's token in Music Assistant and deletes it
  from the console. Do this before removing Afterglow or lending the console.
- **Removing**: log out, close Afterglow, then delete `/data/homebrew/PPSA99481` (from a checkout,
  `make undeploy PS5_HOST=<console address>`). Deleting the folder does not unregister the title: the home-screen
  tile can stay until your enabler refreshes its list.
- **Several consoles**: give each its own `client_id` (and `name`) in `afterglow.conf`, sign in on each, and run the
  scan on each.

## 14. Lyrics and privacy

The Lyrics view shows the lyrics Music Assistant has for the song: timed lyrics follow the music line by line, word
by word when the lyrics carry word timing, and plain lyrics are paged with up and down.

When Music Assistant has no lyrics, the PS5 asks [lrclib.net](https://lrclib.net) directly, sending the song's title
and artist. This is the only request Afterglow makes to a service other than your Music Assistant server (and your
Home Assistant, if you set up the Home section). **Online lyrics** turns it off; it is on by default. **Retry
lyrics** asks again after a failed or empty lookup.
<!-- TODO(owner): check these rows' names and place, and the fields sent to LRCLIB, after the lyrics lane merges. -->

There is no analytics or telemetry.

## 15. When something goes wrong

- **"Ready when you are" with nothing happening**: Afterglow has no server yet. Sign in (section 6) or set `host` in
  `afterglow.conf`.
- **Connecting forever**: check the address is Music Assistant's numeric IPv4 address and that port 8927 is reachable
  from the console's network.
- **Afterglow PS5 never appears in Music Assistant**: see the Sendspin note in section 2.
- **Following shows nothing**: is the MilkDrop Visualizer plugin added, and is the speaker one Music Assistant
  streams to itself?
- **Settings → Status** shows the connection, the buffer, underruns, late frames, reconnects, output errors, the
  frame rate and the display's refresh rate.
- While Afterglow runs, its log is in the title's storage; over FTP it is
  `/mnt/sandbox/PPSA99481_000/download0/hui/dev/app.log`. It helps with a bug report; it names your server's
  LAN address, so look it over before you share it.
  <!-- TODO(owner): confirm app.log never prints user names or tokens. -->
