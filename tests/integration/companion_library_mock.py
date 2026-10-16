#!/usr/bin/env python3
# Afterglow - Disposable Music Assistant fixture for library paging, the queue window and grouping.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

import base64
import hashlib
import os
import select
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from companion_mock_server import exact, receive, send  # noqa: E402


def item(kind, number):
    return {"uri": f"spotify--x://{kind}/{number}", "name": f"{kind[0].upper()}{number}",
            "media_type": kind, "item_id": str(number), "provider": "spotify--x",
            "artists": [{"name": "Fixture Artist"}]}


def exercise(binary):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    process = subprocess.Popen([binary, str(listener.getsockname()[1])])
    connection = None
    player = {"player_id": "speaker", "name": "Fixture Speaker", "type": "player", "available": True,
              "group_members": [], "can_group_with": ["kitchen"]}
    kitchen = {"player_id": "kitchen", "name": "Kitchen", "type": "player", "available": True,
               "synced_to": None, "group_members": [], "can_group_with": ["speaker"]}
    track = {"uri": "library://track/fixture", "name": "Fixture", "artists": [{"name": "Artist"}]}
    queue = {"queue_id": "speaker", "state": "paused", "current_index": 0, "elapsed_time": 1, "items": 120,
             "current_item": {"queue_item_id": "item", "media_item": track, "duration": 60,
                              "streamdetails": {"audio_format": {"content_type": "flac", "sample_rate": 44100,
                                                                 "bit_depth": 16, "bit_rate": 0},
                                                "audio_processing": {"input_fidelity": {"quality": "lossless"}}}}}
    seen = {"fallback": 0, "group": 0, "windows": set()}
    started = time.monotonic()
    try:
        while process.poll() is None and time.monotonic() - started < 25:
            if connection is None:
                if not select.select([listener], [], [], .1)[0]:
                    continue
                connection, _ = listener.accept()
                connection.settimeout(3)
                request = b""
                while not request.endswith(b"\r\n\r\n"):
                    request += exact(connection, 1)
                    assert len(request) < 8192
                headers = {line.split(b":", 1)[0].lower(): line.split(b":", 1)[1].strip()
                           for line in request.split(b"\r\n")[1:] if b":" in line}
                accept = base64.b64encode(hashlib.sha1(headers[b"sec-websocket-key"] +
                                         b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
                connection.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                   b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
                send(connection, {"server_version": "fixture"})
            if not select.select([connection], [], [], .05)[0]:
                continue
            try:
                request = receive(connection)
            except (EOFError, ConnectionResetError):
                connection.close()
                connection = None
                continue
            command, args = request["command"], request.get("args", {})
            result = None
            if command == "auth":
                result = {"authenticated": True}
            elif command == "auth/me":
                result = {"preferences": {}}
            elif command == "players/all":
                result = [player, kitchen]
            elif command == "player_queues/all":
                result = [queue]
            elif command == "player_queues/get":
                result = queue
            elif command == "music/item_by_uri":
                result = track
            elif command == "metadata/get_track_lyrics":
                result = ["", ""]
            elif command == "player_queues/items":
                assert args["limit"] == 50
                seen["windows"].add(args["offset"])
                if args["offset"] == 51 and queue["current_index"] == 0:
                    # The next song starts while a later window is shown.
                    queue["current_index"] = 5
                    queue["current_item"] = dict(queue["current_item"], queue_item_id="item5")
                result = [{"queue_item_id": f"q{n}", "name": f"Q{n}", "media_item": {"name": f"Q{n}"}}
                          for n in range(args["offset"], min(120, args["offset"] + 50))]
            elif command == "music/artists/artist_albums":
                if args["provider_instance_id_or_domain"] == "library":
                    assert args["item_id"] == "426"
                    result = []
                else:
                    assert args == {"item_id": "sp-426", "provider_instance_id_or_domain": "spotify--x"}
                    seen["fallback"] += 1
                    result = [item("album", n) for n in range(70)]
            elif command == "music/artists/get":
                assert args == {"item_id": "426", "provider_instance_id_or_domain": "library"}
                result = {"item_id": "426", "provider": "library", "name": "Fixture Artist",
                          "provider_mappings": [{"item_id": "sp-426", "provider_instance": "spotify--x",
                                                 "available": True}]}
            elif command == "music/search":
                assert args["search_query"] == "fixture" and args["limit"] == 20
                result = {"tracks": [item("track", 1)], "artists": [item("artist", 2)],
                          "albums": [item("album", 3)], "radio": [], "genres": []}
            elif command == "players/cmd/set_members":
                assert args == {"target_player": "speaker", "player_ids_to_add": ["kitchen"]}, args
                seen["group"] += 1
            else:
                raise AssertionError("Unexpected command: " + command)
            send(connection, {"message_id": request["message_id"], "result": result})
        assert process.wait(timeout=3) == 0
        assert seen["fallback"] == 2 and seen["group"] == 1, seen
        assert {1, 51, 6} <= seen["windows"], seen
        print("PASS: library artist provider fallback, appended second page, search sections, "
              "50-item queue window and paging, window resets after the new playing song, set_members grouping, lossless badge")
    finally:
        if connection:
            connection.close()
        listener.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    exercise(sys.argv[1])
