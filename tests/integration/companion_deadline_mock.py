#!/usr/bin/env python3
# Afterglow - Disposable Music Assistant fixture that leaves some requests unanswered.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The socket stays busy with events throughout, so only the Companion's own
# per-request deadlines can notice that a reply never came.

import os
import select
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from account_mock_server import receive, send, upgrade  # noqa: E402


def exercise(binary):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    process = subprocess.Popen([binary, str(listener.getsockname()[1])])
    preferences = {"unrelated": {"keep": 42}, "visualizer_favorites": ["TV"]}
    counts = {}
    held_save = None
    connections = 0
    connection = None
    next_event = 0
    started = time.monotonic()
    try:
        while process.poll() is None and time.monotonic() - started < 60:
            if connection is None:
                if not select.select([listener], [], [], .1)[0]:
                    continue
                connection, _ = listener.accept()
                connection.settimeout(3)
                upgrade(connection)
                connections += 1
            if time.monotonic() >= next_event:
                send(connection, {"event": "queue_time_updated", "object_id": "elsewhere",
                                  "data": 1})
                next_event = time.monotonic() + .1
            if not select.select([connection], [], [], .05)[0]:
                continue
            try:
                request = receive(connection)
            except (EOFError, ConnectionResetError):
                connection.close()
                connection = None
                continue
            command, args = request["command"], request.get("args", {})
            counts[command] = counts.get(command, 0) + 1
            number = counts[command]
            reply = {"message_id": request["message_id"]}
            if command == "auth":
                reply["result"] = {"authenticated": True}
            elif command == "auth/me":
                if number == 1:
                    continue  # never answered
                reply["result"] = {"preferences": preferences}
            elif command == "music/recently_played_items":
                if number == 1:
                    continue  # never answered
                reply["result"] = [{"item_id": "1", "name": "Recent", "uri": "library://track/1",
                                    "media_type": "track"}]
            elif command == "auth/user/update":
                if number == 1:
                    held_save = request["message_id"]
                    continue  # answered only once the retry is in flight
                if number == 2:
                    # The first save's success arrives late, while the second is
                    # in flight: it must not count as the second's answer.
                    send(connection, {"message_id": held_save,
                                      "result": {"preferences": args["preferences"]}})
                    reply["error_code"] = 500
                    reply["details"] = "fixture refusal"
                else:
                    preferences = args["preferences"]
                    reply["result"] = {"preferences": preferences}
            elif command in ("players/all", "player_queues/all"):
                reply["result"] = []
            else:
                raise AssertionError("Unexpected command: " + command)
            send(connection, reply)
        assert process.wait(timeout=5) == 0, "smoke exited with %s" % process.returncode
        assert connections == 1, "deadlines must not cost the connection: %d" % connections
        assert counts.get("auth/user/update") == 3, counts
        assert set(preferences["visualizer_favorites"]) == {"TV", "Native"}, preferences
        assert preferences["unrelated"] == {"keep": 42}
        assert counts.get("music/recently_played_items") == 2, counts
        print("PASS: an unanswered account read, favourite save and recent read each time out on "
              "a busy socket, a late reply is ignored, the edit is kept and saved on a retry")
    finally:
        if connection:
            connection.close()
        listener.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    exercise(sys.argv[1])
