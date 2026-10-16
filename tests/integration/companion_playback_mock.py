#!/usr/bin/env python3
# Afterglow - Disposable Music Assistant fixture for the command target, transfers and recent music.
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


def player(player_id, name, **extra):
    value = {"player_id": player_id, "name": name, "type": "player", "available": True,
             "enabled": True, "hide_in_ui": False, "synced_to": None, "group_members": [],
             "can_group_with": [], "supported_features": ["volume_set", "set_members"],
             "volume_level": 30}
    value.update(extra)
    return value


def exercise(binary):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    process = subprocess.Popen([binary, str(listener.getsockname()[1])])
    connection = None
    # The living room plays (the kitchen synced to it), the bedroom and this
    # PS5 are idle with empty queues. Transfers move the one song around.
    players = {
        "living": player("living", "Living room", group_members=["living", "kitchen"],
                         group_volume=40, can_group_with=["bedroom", "kitchen"]),
        "kitchen": player("kitchen", "Kitchen", synced_to="living"),
        "bedroom": player("bedroom", "Bedroom"),
        "ps5": player("ps5", "Afterglow PS5"),
    }
    song = {"queue_item_id": "item", "duration": 200,
            "media_item": {"uri": "library://track/1", "name": "Fixture Song",
                           "artists": [{"name": "Fixture Artist"}]}}
    queues = {qid: {"queue_id": qid, "state": "idle", "current_index": None, "elapsed_time": 0,
                    "items": 0, "current_item": None} for qid in ("living", "bedroom", "ps5")}
    queues["living"].update(state="playing", current_index=0, items=1, current_item=song)
    seen = {"commands": [], "transfers": [], "volumes": [], "recent": 0, "media": []}

    def move(source, target, auto_play):
        moved, empty = dict(queues[source]), queues[target]
        state = "playing" if auto_play else moved["state"]
        queues[target] = dict(moved, queue_id=target, state=state)
        queues[source] = dict(empty, queue_id=source)

    started = time.monotonic()
    try:
        while process.poll() is None and time.monotonic() - started < 40:
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
                assert request.startswith(b"GET /ws "), request[:40]
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
                result = list(players.values())
            elif command == "player_queues/all":
                result = list(queues.values())
            elif command == "player_queues/get":
                result = queues[args["queue_id"]]
            elif command == "player_queues/items":
                result = []
            elif command in ("music/item_by_uri", "metadata/get_track_lyrics", "config/players/get"):
                result = None
            elif command == "music/recently_played_items":
                assert args == {"limit": 10, "fully_played_only": False}, args
                seen["recent"] += 1
                result = [{"item_id": "3", "provider": "library", "name": "Harbour Lamps",
                           "uri": "library://album/3", "media_type": "album",
                           "image": {"type": "thumb", "path": "x", "proxy_id": "ab12",
                                     "remotely_accessible": False}},
                          {"item_id": "9", "provider": "library", "name": "Unplayable"}]
            elif command in ("player_queues/play", "player_queues/pause"):
                seen["commands"].append((command, args["queue_id"]))
            elif command == "player_queues/play_media":
                seen["media"].append((args["queue_id"], args["media"], args["option"]))
            elif command in ("players/cmd/group_volume", "players/cmd/volume_set"):
                seen["volumes"].append((command, args["player_id"], args["volume_level"]))
            elif command == "player_queues/transfer":
                assert set(args) <= {"source_queue_id", "target_queue_id", "auto_play"}, args
                seen["transfers"].append((args["source_queue_id"], args["target_queue_id"],
                                          args.get("auto_play")))
                move(args["source_queue_id"], args["target_queue_id"], args.get("auto_play"))
            else:
                raise AssertionError("Unexpected command: " + command)
            send(connection, {"message_id": request["message_id"], "result": result})
        assert process.wait(timeout=3) == 0
        assert seen["volumes"][0] == ("players/cmd/group_volume", "living", 45), seen
        assert ("players/cmd/volume_set", "bedroom", 25) in seen["volumes"], seen
        assert ("players/cmd/volume_set", "living", 35) in seen["volumes"], seen
        kitchen = [level for command, player, level in seen["volumes"] if player == "kitchen"]
        assert kitchen and kitchen[-1] == 40 and all(level <= 40 for level in kitchen), seen
        assert seen["recent"] == 1, seen
        assert ("player_queues/play", "bedroom") in seen["commands"], seen
        assert seen["media"] == [("bedroom", "library://album/3", "replace")], seen
        assert seen["transfers"] == [("living", "bedroom", None), ("bedroom", "ps5", True)], seen
        print("PASS: group volume on the followed leader, each speaker's own volume, recent music, Play and Play now on a "
              "chosen silent speaker, transfer to a silent speaker, Play on this PS5 with auto_play")
    finally:
        if connection:
            connection.close()
        listener.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    exercise(sys.argv[1])
