#!/usr/bin/env python3
# Afterglow - Disposable Music Assistant fixture for combined presses, token hand-over and the relay stall guard.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

import base64
import hashlib
import json
import select
import socket
import struct
import subprocess
import sys
import time

REPLY_DELAY = 0.4  # every write is answered late, so presses pile up behind it


def exact(connection, count):
    data = b""
    while len(data) < count:
        part = connection.recv(count - len(data))
        if not part:
            raise EOFError
        data += part
    return data


def frame(opcode, data):
    if len(data) < 126:
        return bytes([128 | opcode, len(data)]) + data
    return bytes([128 | opcode, 126]) + struct.pack(">H", len(data)) + data


def send(connection, message):
    connection.sendall(frame(1, json.dumps(message).encode()))


def receive(connection):
    first, second = exact(connection, 2)
    assert second & 128, "client frames must be masked"
    length = second & 127
    if length == 126:
        length = struct.unpack(">H", exact(connection, 2))[0]
    mask = exact(connection, 4)
    data = bytes(value ^ mask[i % 4] for i, value in enumerate(exact(connection, length)))
    opcode = first & 15
    if opcode == 8:
        raise EOFError
    return json.loads(data) if opcode == 1 else None


def upgrade(connection):
    request = b""
    while not request.endswith(b"\r\n\r\n"):
        request += exact(connection, 1)
        assert len(request) < 8192
    path = request.split(b" ")[1].decode()
    headers = {line.split(b":", 1)[0].lower(): line.split(b":", 1)[1].strip()
               for line in request.split(b"\r\n")[1:] if b":" in line}
    accept = base64.b64encode(hashlib.sha1(headers[b"sec-websocket-key"] +
                                           b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
    connection.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                       b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
    return path


def wave(timestamp_us):
    return frame(2, bytes([22]) + struct.pack(">q", timestamp_us) + bytes([128] * 1024))


class Fixture:
    def __init__(self, mode):
        self.mode = mode
        self.started = time.monotonic()
        # A sync group led by "leader": its own volume_level is the leader's, the group's is 40.
        self.leader = {"player_id": "leader", "name": "Fixture Leader", "type": "player",
                       "available": True, "volume_level": 70, "group_volume": 40,
                       "group_members": ["leader", "member"]}
        self.member = {"player_id": "member", "name": "Fixture Member", "type": "player",
                       "available": True, "volume_level": 20, "group_volume": 20,
                       "synced_to": "leader", "group_members": []}
        track = {"uri": "library://track/fixture", "name": "Fixture",
                 "artists": [{"name": "Artist"}]}
        self.queue = {"queue_id": "leader", "state": "playing" if mode == "relay" else "paused",
                      "current_index": 0, "elapsed_time": 12.75,
                      "current_item": {"queue_item_id": "item", "media_item": track, "duration": 240}}
        self.tokens = {"fixture-token"}
        self.writes = []
        self.later = []  # (due, connection, message)
        self.api = None
        self.dropped_at = None
        self.refused_old = False
        self.fresh_auth = False
        self.relays = []  # [connection, opened, frames until, closed]
        self.paused_at = None
        self.track = track

    def now(self):
        return time.monotonic() - self.started

    def answer(self, connection, request):
        command, args = request["command"], request.get("args", {})
        reply = lambda result, delay=0: self.later.append(
            (time.monotonic() + delay, connection, {"message_id": request["message_id"], "result": result}))
        if command == "auth":
            token = args.get("token")
            if token == "fresh-token":
                self.fresh_auth = True
            if token in self.tokens:
                return reply({"authenticated": True})
            # The old token, refused late: the console renews meanwhile.
            self.refused_old = True
            return reply({"authenticated": False}, 2.0)
        if command == "auth/me":
            return reply({"preferences": {}})
        if command == "players/all":
            return reply([self.leader, self.member])
        if command == "player_queues/all":
            return reply([self.queue])
        if command == "player_queues/get":
            return reply(self.queue)
        if command == "music/item_by_uri":
            return reply(self.track)
        if command == "metadata/get_track_lyrics":
            return reply(["", ""])
        if command == "player_queues/items":
            return reply([])
        assert command.startswith("players/cmd/") or command.startswith("player_queues/"), command
        self.writes.append((command, args))
        return reply(None, REPLY_DELAY)

    def controls_phase(self):
        # Once every press has been sent, Music Assistant drops the socket (as a
        # revoke may) and from then on refuses the old token.
        done = [c for c, _ in self.writes]
        if self.dropped_at is None and done.count("player_queues/pause") == 1 and not self.later:
            self.dropped_at = self.now()
            self.tokens = {"fresh-token"}
            self.api.close()
            self.api = None

    def relay_phase(self, connections):
        if len(self.relays) == 2 and self.paused_at is None and self.now() > self.relays[1][2]:
            # The followed speaker pauses: its frames stop, its queue says so.
            self.paused_at = self.now()
            self.queue["state"] = "paused"
            if self.api:
                send(self.api, {"event": "queue_updated", "object_id": "leader", "data": self.queue})
        for relay in self.relays:
            if relay[3] is None and self.now() < relay[2]:
                relay[0].sendall(wave(int(self.now() * 1e6)))

    def check(self):
        if self.mode == "controls":
            volumes = [a["volume_level"] for c, a in self.writes if c.endswith("volume")]
            assert all(c != "players/cmd/volume_set" for c, _ in self.writes), self.writes
            # 40 + 5 in flight, then the three waiting steps from 45: never 40 + 5 again.
            assert volumes == [45, 60], volumes
            seeks = [a["position"] for c, a in self.writes if c == "player_queues/seek"]
            assert seeks == [43], seeks
            skips = [c for c, _ in self.writes if c == "player_queues/next"]
            assert len(skips) == 1, skips
            toggles = [c for c, _ in self.writes if c in ("player_queues/play", "player_queues/pause")]
            assert toggles == ["player_queues/play", "player_queues/pause"], toggles
            assert self.dropped_at is not None and self.refused_old and self.fresh_auth
            print("PASS: held volume adds up on players/cmd/group_volume (%s), quick seeks add up (%s), "
                  "five skips send %d, a second toggle undoes the first before the report, the old "
                  "token's late refusal logs in again with the renewed one"
                  % (volumes, seeks, len(skips)))
        else:
            assert len(self.relays) == 2, "one stall reconnect, then nothing: %d relays" % len(self.relays)
            stall = self.relays[1][1] - self.relays[0][2]
            assert 9.5 < stall < 13, "reconnected %.1f s after the frames stopped" % stall
            assert self.relays[1][3] is not None and self.relays[1][3] - self.paused_at < 1.5, \
                "the paused speaker's relay closes"
            print("PASS: frames that stop mid-stream reconnect the relay after %.1f s; a speaker that "
                  "pauses closes it and is never reconnected" % stall)


def exercise(binary, mode):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(8)
    process = subprocess.Popen([binary, str(listener.getsockname()[1]), mode])
    fixture = Fixture(mode)
    connections = {}
    limit = 40
    try:
        while process.poll() is None and fixture.now() < limit:
            readable = select.select([listener] + list(connections), [], [], .05)[0]
            for item in readable:
                if item is listener:
                    connection, _ = listener.accept()
                    connection.settimeout(3)
                    path = upgrade(connection)
                    if path == "/ws":
                        fixture.api = connection
                        connections[connection] = "api"
                        send(connection, {"server_version": "fixture"})
                    else:
                        assert path == "/milkdrop_visualizer?player=leader", path
                        connections[connection] = "relay"
                        # Two seconds of frames on each socket, then silence with no stream/end.
                        fixture.relays.append([connection, fixture.now(), fixture.now() + 2, None])
                    continue
                try:
                    request = receive(item)
                except (EOFError, ConnectionError, OSError):
                    for relay in fixture.relays:
                        if relay[0] is item and relay[3] is None:
                            relay[3] = fixture.now()
                    del connections[item]
                    item.close()
                    if fixture.api is item:
                        fixture.api = None
                    continue
                if request is None:
                    continue
                if connections[item] == "relay":
                    if request.get("type") == "auth":
                        send(item, {"type": "auth_ok"})
                    continue
                fixture.answer(item, request)
            now = time.monotonic()
            due = [entry for entry in fixture.later if entry[0] <= now]
            fixture.later = [entry for entry in fixture.later if entry[0] > now]
            for _, connection, message in due:
                if connection in connections:
                    send(connection, message)
            if mode == "controls" and fixture.api is not None:
                before = fixture.api
                fixture.controls_phase()
                if fixture.api is None:
                    connections.pop(before, None)
            if mode == "relay":
                fixture.relay_phase(connections)
        assert process.wait(timeout=5) == 0, "smoke exited with %s" % process.returncode
        fixture.check()
    finally:
        for connection in connections:
            connection.close()
        listener.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    exercise(sys.argv[1], sys.argv[2])
