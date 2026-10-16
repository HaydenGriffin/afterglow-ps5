#!/usr/bin/env python3
# Afterglow - Disposable Music Assistant auth fixture; never contacts a real server.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

import base64
import datetime
import hashlib
import json
import select
import socket
import struct
import subprocess
import sys
import tempfile
import os
import time

NAME = "Afterglow on PS5 (afterglow-ps5-test)"


def exact(connection, count):
    data = b""
    while len(data) < count:
        part = connection.recv(count - len(data))
        if not part:
            raise EOFError
        data += part
    return data


def send(connection, message):
    # Compact, like Music Assistant's own serializer.
    data = json.dumps(message, separators=(",", ":")).encode()
    header = bytes([129, len(data)]) if len(data) < 126 else b"\x81\x7e" + struct.pack(">H", len(data))
    connection.sendall(header + data)


def receive(connection):
    first, second = exact(connection, 2)
    assert first == 129 and second & 128, "client frames must be final, text and masked"
    length = second & 127
    if length == 126:
        length = struct.unpack(">H", exact(connection, 2))[0]
    mask = exact(connection, 4)
    data = exact(connection, length)
    return json.loads(bytes(value ^ mask[i % 4] for i, value in enumerate(data)))


def upgrade(connection):
    request = b""
    while not request.endswith(b"\r\n\r\n"):
        request += exact(connection, 1)
        assert len(request) < 8192
    assert request.startswith(b"GET /ws HTTP/1.1\r\n")
    headers = {line.split(b":", 1)[0].lower(): line.split(b":", 1)[1].strip()
               for line in request.split(b"\r\n")[1:] if b":" in line}
    accept = base64.b64encode(hashlib.sha1(headers[b"sec-websocket-key"] +
                                           b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
    connection.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                       b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
    send(connection, {"server_version": "fixture", "schema_version": 65})


def stamp(days):
    moment = datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=days)
    return moment.isoformat()


class Server:
    def __init__(self):
        self.tokens = {"other": {"token": "other-secret", "name": "viz-man on Living Room C1",
                                 "long": True, "created": stamp(-5), "expires": stamp(360)}}
        self.created = 0
        self.log = []
        self.revoked = []

    def handle(self, session, command, args):
        self.log.append(command)
        if command == "auth/login":
            if args.get("username") != "fixture" or args.get("password") != "secret":
                return {"success": False, "error": "Invalid username or password"}
            assert args.get("device_name") == NAME
            self.tokens["session"] = {"token": "session-secret", "name": NAME, "long": False,
                                      "created": stamp(0), "expires": stamp(1)}
            return {"success": True, "access_token": "session-secret",
                    "user": {"username": "Fixture User"}}
        if command == "auth":
            known = any(t["token"] == args.get("token") for t in self.tokens.values())
            session["authed"] = known
            return {"authenticated": known}
        assert session.get("authed"), command + " before auth"
        if command == "auth/token/create":
            assert args == {"name": NAME}
            self.created += 1
            token_id = "long-%d" % self.created
            # The sign-in tokens are close to expiry so the renewal path runs.
            self.tokens[token_id] = {"token": token_id, "name": NAME, "long": True,
                                     "created": stamp(-1 if self.created <= 2 else 0),
                                     "expires": stamp(10 if self.created <= 2 else 365)}
            return token_id
        if command == "auth/tokens":
            return [{"token_id": key, "name": t["name"], "created_at": t["created"],
                     "expires_at": t["expires"], "is_long_lived": t["long"],
                     "token_hash": "x" * 64} for key, t in self.tokens.items()]
        if command == "auth/token/revoke":
            assert args["token_id"] in self.tokens
            del self.tokens[args["token_id"]]
            self.revoked.append(args["token_id"])
            return None
        raise AssertionError("Unexpected command: " + command)


def exercise(binary):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    folder = tempfile.mkdtemp()
    path = os.path.join(folder, "afterglow.auth")
    process = subprocess.Popen([binary, str(listener.getsockname()[1]), path])
    server = Server()
    connections = []
    started = time.monotonic()
    try:
        while process.poll() is None and time.monotonic() - started < 30:
            readable = select.select([listener] + [c for c, _ in connections], [], [], .1)[0]
            for item in readable:
                if item is listener:
                    connection, _ = listener.accept()
                    connection.settimeout(3)
                    upgrade(connection)
                    connections.append((connection, {}))
                    continue
                session = next(s for c, s in connections if c is item)
                try:
                    request = receive(item)
                except (EOFError, ConnectionError, AssertionError):
                    item.close()
                    connections = [(c, s) for c, s in connections if c is not item]
                    continue
                result = server.handle(session, request["command"], request.get("args", {}))
                send(item, {"message_id": request["message_id"], "result": result})
        assert process.wait(timeout=5) == 0, "smoke exited with %s" % process.returncode
        ours = sorted(k for k, t in server.tokens.items() if t["name"] == NAME)
        assert ours == [], "log out must revoke every token in this name: %s" % ours
        assert "other" in server.tokens, "another device's token must survive"
        # Second sign-in: its session and the superseded long-1, never the new long-2.
        assert server.revoked == ["session", "session", "long-1", "long-2", "long-3"], server.revoked
        assert not os.path.exists(path), "log out removes the token file"
        print("PASS: wrong password refused, sign-in with session revoke, re-sign-in revokes the "
              "superseded token only, renewal within 30 days "
              "revokes the older token, log out revokes every token in this name and removes the file")
    finally:
        for connection, _ in connections:
            connection.close()
        listener.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    exercise(sys.argv[1])
