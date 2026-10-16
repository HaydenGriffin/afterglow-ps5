# Native Sendspin smoke test

Run from the repository root on macOS or Linux with a C++20 compiler and Python 3:

```sh
bash tests/integration/run-sendspin-smoke.sh
```

To reproduce the console policy that permits `F_GETFL` but denies `F_SETFL`
with `EACCES`, run the same real-network fixture with a test-only `fcntl` shim:

```sh
AFTERGLOW_TEST_DENY_FCNTL=1 bash tests/integration/run-sendspin-smoke.sh
```

The host transport must still connect using `ioctl(FIONBIO)` and shut down
promptly. The PS5 build uses the console's `SO_NBIO` socket option instead, which
only hardware can verify.

Set `CXX=clang++-18` if the default compiler lacks C++20 support. The script builds
the actual native client and queue, then runs a disposable server on an ephemeral
loopback port. It does not contact Music Assistant, a console, or existing players.

The test checks the WebSocket upgrade and masked client frames, PCM-only format
negotiation, timestamp synchronization, signed stereo PCM output, replacing queued
audio after a seek, Unicode metadata, fragmented frames with interleaved ping/pong,
the next-track command, and stopping the network worker within 500 ms. It takes
about three seconds after compilation. It does not validate PS5 HDMI output,
hardware latency, long-running clock drift, or playback alongside a game.

The deterministic queue and malformed-packet tests run with the normal unit suite:

```sh
make test-unit GTEST_ARGS='--gtest_filter=Afterglow*'
```

## Sign-in and account mirroring smoke

```sh
bash tests/integration/run-account-smoke.sh
```

Two disposable loopback servers stand in for Music Assistant. The first checks
console sign-in (wrong password refused, `auth/login` then a long-lived token
named `Afterglow on PS5 (<client_id>)`, the session token revoked), renewal
inside 30 days of expiry (older token revoked) and log-out (every token in that
name revoked, the token file removed). The second checks the Companion's
settings mirroring (`vizman.devices[<device>]` read, a debounced
merge-before-write that keeps other devices, favourites and unrelated keys) and
the static delay (found on the universal player under a protocol-prefixed key,
then saved with `config/players/save`). Neither contacts a real server.

## Command target and moving music smoke

```sh
bash tests/integration/run-companion-playback-smoke.sh
```

A disposable loopback server stands in for Music Assistant 2.10.6 with a
playing living room (a kitchen synced to it), an idle bedroom and this PS5's
own player. It checks the command target (the followed queue, then a chosen
silent speaker that still takes Play, Play now and its own volume), group
volume on a sync leader (`players/cmd/group_volume`), recent music for the idle
poster (`music/recently_played_items`, artwork made absolute), a transfer to a
silent speaker (`player_queues/transfer` without `auto_play`) and Play on this
PS5 (`auto_play: true`). It never contacts a real server.
