# Music over games

Afterglow can play Music Assistant over a game, with the app closed, through the console's own USB
music player. This page says how, and what was measured to get there. Setup is in
[install.md](install.md#16-music-over-games).

Everything here was found on **one PS5 on system software 7.20**, with ELF loader, kstuff and
ShadowMount Plus running. Other firmware may differ, and the payload says so if it cannot follow the
player.

## How it works

A title's own audio stops when it leaves the foreground, and a payload's audio port is silent. The
USB music player (`SystemMusicCore.elf`) is the one thing that keeps playing over games, so the
payload `afterglow-music.elf` gives it something to play:

1. It joins Music Assistant as its own speaker, **Afterglow over games**, using the same Sendspin
   client as the app.
2. It keeps 32 files in `Music/Afterglow Live/` on a USB drive. Each holds some seconds of silence
   until a song is written over it.
3. Each song goes into a file of its own, so the Music card shows that song's title, artist and
   length. A file is made the song's length before the player opens it, and audio is written as it
   arrives, ahead of where the player is reading.
4. When the player reaches the end of a file it goes on to the next, which by then holds the next
   song.
5. The payload watches which file the player has open, and where it is reading. That tells it about
   Next, Previous and Pause on the card, and it passes them on to Music Assistant.

The technique of writing a FLAC file ahead of the player, and the frame format, come from
[librespot-ps5](https://github.com/stupiditytries/librespot-ps5) v0.1.0. One file per song, and
watching the player's open files, are Afterglow's.

## What the player does

These came from test files and from watching the player's file table, on 3 November 2026.

| Behaviour | Measured |
| --- | --- |
| How far it reads ahead of what is heard | 9.7 s, whichever block size from 1024 to 4096 |
| Moving to the next file by itself | 0.2 to 0.3 s between closing one and opening the next |
| Moving after a press of Next or Previous | about 0.5 s, then up to 4 s more before sound |
| At the end of the folder | with Repeat on All it goes back to the first file; off, it stops there |
| A header claiming more or less than the file holds | it plays what is there and goes on |
| A header claiming no length at all | it plays what is there, but the card says 26:00:00 |
| Next and Previous | move one file each way, even 10 s into a track |
| Pause | keeps the file open and stops reading |
| Tags | read for the next file once the current one has been read to its end |
| Size of a file | taken when it opens: reading stops there, however much the file grows after |
| A file cut shorter than that size while open | the player stops and leaves the folder |
| Blank bytes inside a file | read at normal speed and heard as silence, so they do not end it |
| Blocks of 256 samples | it cannot keep up: a 20 s file took 31 s |
| Artwork in a file | never shown on the card |

Those shape the design:

- **A file's length is settled before it opens.** It cannot be grown, shrunk or ended early once the
  player has it. So each file is sized to what Music Assistant says is left of the song, the audio
  runs on into the next file at that point, and nothing is cut while it is open.
- **The song is named late.** Music Assistant names a new song about 1.3 s after its audio starts,
  and audio goes in 2 s ahead of its time. The next file starts with a name left over, and is renamed
  when the new song is named, 3.3 s after the file began. The player reads the end of the file before
  it 6.3 s after that file was finished, at the default lead of 16 s, so the name gets there first.
- **A file ends where its song does.** When a song is named, the server's count says where in the file
  it began, so its file is sized to end with it, and one late start does not carry over to the songs
  after it.
- **A waiting file is 14 s long.** The player cannot read its end for 4.3 s, which leaves time for
  Music Assistant to answer a press.
- **Writes are large and few.** Topping up the silence a frame at a time made about 24 writes a
  second, and a stick fell behind after about 9.5 minutes. Writing it in 6 s steps makes about 1.15 a
  second. A 1 h 55 min run in the single-file design that came before this had no dropped audio or
  jumps.

## What does not work

- **Starting the player without a press.** A payload cannot open the controller
  (`scePadOpen` returns `0x809b0081`, even with raised authids), and librespot-ps5 reports that asking
  the media service to play from a payload crashes it. The press is needed once; after that the
  player stays on the ring, even while the payload restarts.
- **Remote Play** blocks the player.
- **Gapless albums** get a short break between songs, where the player changes file.
- **Skips from elsewhere** (the phone, the Afterglow app) cannot end the file the player is on. The new
  song plays straight away, a toast names it, and the card catches up at the end of that file, which
  can be minutes. Next on the card has no such wait.
- **Covers on the card.** Afterglow shows them nowhere the player does not: the toast uses Afterglow's
  icon for now.

## Reading the player's file table

`payload/player_watch.hpp` walks `SystemMusicCore.elf`'s descriptors through the SDK's kernel reads.
It only reads, and checks every pointer before following it. `sysctl` with `KERN_PROC_FILEDESC` is
gone on this firmware (`ENOENT`). The offsets read off 7.20 are:

| Structure | Offset | Field |
| --- | --- | --- |
| descriptor table | 0x8 + 0x30 per descriptor | the file |
| file | 0x18 | vnode |
| file | 0x20 | type (1 is a vnode) |
| file | 0x24 | flags (1 is read-only) |
| file | 0x30 | next read position; moves with `pread` too |
| vnode | 0x10 | private data |
| null mount's private data | 0x10 | the vnode beneath |

The player reads through a null mount, so its vnode is matched to the payload's by the one beneath.
At start the payload reads a file of its own and checks the read position is where it should be;
if not, it shows a toast and exits.

## Files and ports

| What | Where |
| --- | --- |
| Song files | `<drive>/Music/Afterglow Live/Afterglow 01.flac` to `32.flac`, about 86 MB in all, plus the current songs |
| Log, and optional settings | `/data/afterglow-music/music.log`, `music.conf` |
| Control port | `127.0.0.1:9119`: a line in (`status`, `stop`), a line out |
| ELF loader the app uses | `127.0.0.1:9021` |

`music.conf` is optional. `host`, `port`, `drive`, `lead`, `status`, `icon`, `toasts` and `card` can be
set there, one `key=value` per line. The app's address wins over the file's.

The app starts the payload by handing the loader a copy with the server's address written into it,
because a sandboxed title has no file the payload can read. The two only ever speak on loopback.

## Developing

`tools/build-music-payload.sh` builds the payload by itself, and `make app` puts it in the app folder.
The platform-neutral parts are checked by `tests/unit/music_over_games_test.cpp`.

Do not read a song file over FTP while it plays: that appears to have caused a dropout. Use the log.
Each line gives `play` and `write` (the files), `ahead` (lead in seconds), `spare_min` (the least the
write head was ahead of the read head) and `queued` (seconds of writing not yet on the drive).
