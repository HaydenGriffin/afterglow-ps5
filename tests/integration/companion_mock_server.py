#!/usr/bin/env python3
# Afterglow - Disposable Music Assistant fixture; never contacts real speakers.
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


def exact(connection, count):
    data = b""
    while len(data) < count:
        part = connection.recv(count - len(data))
        if not part:
            raise EOFError
        data += part
    return data


def send(connection, message):
    data = json.dumps(message).encode()
    header = bytes([129, len(data)]) if len(data) < 126 else b"\x81\x7e" + struct.pack(">H", len(data))
    connection.sendall(header + data)


def receive(connection):
    first, second = exact(connection, 2)
    assert first == 129 and second & 128
    length = second & 127
    if length == 126:
        length = struct.unpack(">H", exact(connection, 2))[0]
    assert length < 65536
    mask = exact(connection, 4)
    data = exact(connection, length)
    return json.loads(bytes(value ^ mask[i % 4] for i, value in enumerate(data)))


def exercise(binary, mode=None):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    process = subprocess.Popen([binary, str(listener.getsockname()[1])] + ([mode] if mode else []))
    connection = None
    preferences = {"unrelated": {"keep": 42}, "visualizer_favorites": ["Old", "TV"],
                   "vizman": {"mirrored": ["Old", "TV"], "devices": {"lg": {"dim": 40}},
                              "favoritesMeta": {"Old": {"on": True, "at": 1}, "TV": {"on": True, "at": 1}}}}
    player = {"player_id": "speaker", "name": "Fixture Speaker", "type": "player", "available": True}
    track = {"uri": "library://track/fixture", "name": "Fixture", "artists": [{"name": "Artist"}],
             "metadata": {"images": [{"proxy_id": "abc"}]}}
    queue = {"queue_id": "speaker", "state": "paused", "current_index": 0, "elapsed_time": 12.75,
             "current_item": {"queue_item_id": "item", "media_item": track, "duration": 60}}
    saves = authentications = connections = 0
    started = time.monotonic()
    try:
        while process.poll() is None and time.monotonic() - started < 12:
            if connection is None:
                if not select.select([listener], [], [], .1)[0]:
                    continue
                connection, _ = listener.accept()
                connection.settimeout(3)
                request = b""
                while not request.endswith(b"\r\n\r\n"):
                    request += exact(connection, 1)
                    assert len(request) < 8192
                assert request.startswith(b"GET /ws HTTP/1.1\r\n"), "Metadata-only must not open a relay"
                headers = {line.split(b":", 1)[0].lower(): line.split(b":", 1)[1].strip()
                           for line in request.split(b"\r\n")[1:] if b":" in line}
                accept = base64.b64encode(hashlib.sha1(headers[b"sec-websocket-key"] +
                                         b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
                connection.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                   b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
                send(connection, {"server_version": "fixture"})
                connections += 1
            if not select.select([connection], [], [], .05)[0]:
                continue
            try:
                request = receive(connection)
            except EOFError:
                connection.close()
                connection = None
                continue
            command = request["command"]
            args = request.get("args", {})
            result = None
            if command == "auth":
                assert args == {"token": "fixture-token"}
                authentications += 1
                result = {"authenticated": True}
            elif command == "auth/me":
                result = {"preferences": preferences}
            elif command == "players/all":
                result = [player]
            elif command == "player_queues/all":
                result = [queue]
            elif command == "player_queues/get":
                result = queue
            elif command == "music/item_by_uri":
                result = track
            elif command == "metadata/get_track_lyrics":
                result = ["", "[00:01.00]Fixture"]
            elif command == "player_queues/items":
                result = [{"name": "Next", "media_item": {"name": "Next"}}]
            elif command == "auth/user/update":
                assert authentications >= 2, "Account must be refreshed before a save"
                preferences = args["preferences"]
                assert preferences["unrelated"] == {"keep": 42}
                assert preferences["vizman"]["devices"] == {"lg": {"dim": 40}}
                assert set(preferences["visualizer_favorites"]) == {"TV", "Native"}
                assert preferences["vizman"]["favoritesMeta"]["Old"]["on"] is False
                saves += 1
                if saves == 1:
                    # The write happened but its acknowledgement is lost. Retrying
                    # after reconnect must preserve both the edit and other devices.
                    connection.close()
                    connection = None
                    continue
                result = {"preferences": preferences}
            else:
                raise AssertionError("Unexpected command: " + command)
            send(connection, {"message_id": request["message_id"], "result": result})
        assert process.wait(timeout=3) == 0
        assert saves >= 2 and connections >= 2
        if mode == "follow":
            print("PASS: following with the relay on keeps a paused speaker on the poster")
            return
        print("PASS: real masked transport, metadata-only pinned paused speaker, decimal elapsed, lyrics, "
              "favorites merge, refresh before save, retry after lost acknowledgement, bounded stop")
    finally:
        if connection:
            connection.close()
        listener.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    exercise(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
