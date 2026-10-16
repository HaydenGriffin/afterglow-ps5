#!/usr/bin/env python3
# Afterglow - Disposable Music Assistant fixture for settings mirroring and static delay.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

import os
import select
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from account_mock_server import receive, send, upgrade  # noqa: E402

KEY = "afterglow-ps5-test||protocol||sendspin_static_delay"


def exercise(binary):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    process = subprocess.Popen([binary, str(listener.getsockname()[1])])
    preferences = {"unrelated": {"keep": 42}, "visualizer_favorites": ["TV"],
                   "vizman": {"mirrored": ["TV"], "favoritesMeta": {"TV": {"on": True, "at": 1}},
                              "devices": {"tv-lg": {"fpsCap": 30},
                                          "ps5-afterglow-ps5-test": {"brightness": 10}}}}
    delay = 80
    saves = delay_saves = refused = 0
    connection = None
    started = time.monotonic()
    try:
        while process.poll() is None and time.monotonic() - started < 40:
            if connection is None:
                if not select.select([listener], [], [], .1)[0]:
                    continue
                connection, _ = listener.accept()
                connection.settimeout(3)
                upgrade(connection)
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
                result = {"authenticated": args == {"token": "fixture-token"}}
                refused += not result["authenticated"]
            elif command == "auth/me":
                result = {"preferences": preferences}
            elif command == "players/all":
                result = [{"player_id": "upd-ps5", "name": "Afterglow PS5", "type": "player",
                           "available": True}]
            elif command == "player_queues/all":
                result = []
            elif command == "config/players/get":
                # The Sendspin player is tried first; here only the universal
                # player carries the protocol-prefixed setting.
                values = {}
                if args["player_id"] == "upd-ps5":
                    values = {KEY: {"key": KEY, "default_value": 0, "value": delay}}
                else:
                    assert args["player_id"] == "afterglow-ps5-test", args
                result = {"player_id": args["player_id"], "values": values}
            elif command == "config/players/save":
                assert args == {"player_id": "upd-ps5", "values": {KEY: 150}}, args
                delay = 150
                delay_saves += 1
                result = {"player_id": "upd-ps5"}
            elif command == "auth/user/update":
                written = args["preferences"]
                assert written["unrelated"] == {"keep": 42}
                assert written["vizman"]["devices"]["tv-lg"] == {"fpsCap": 30}
                assert written["visualizer_favorites"] == ["TV"]
                assert written["vizman"]["devices"]["ps5-afterglow-ps5-test"] == {"brightness": 55}, \
                    "the debounce must coalesce edits into the last value"
                preferences = written
                saves += 1
                result = {"preferences": written}
            else:
                raise AssertionError("Unexpected command: " + command)
            send(connection, {"message_id": request["message_id"], "result": result})
        assert process.wait(timeout=5) == 0, "smoke exited with %s" % process.returncode
        assert saves == 1 and delay_saves == 1, (saves, delay_saves)
        assert refused == 1, "a refused token must not reconnect: %d auth attempts" % refused
        print("PASS: device prefs read, debounced merge-before-write keeps LG data and favourites, "
              "static delay found on the universal player and saved, refused token not retried")
    finally:
        if connection:
            connection.close()
        listener.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    exercise(sys.argv[1])
