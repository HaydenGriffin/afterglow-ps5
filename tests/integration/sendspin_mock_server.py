#!/usr/bin/env python3
# Afterglow - Local-only Sendspin protocol integration fixture.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

import argparse
import base64
import hashlib
import json
import select
import socket
import struct
import subprocess
import time


def exercise(binary):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(5)
    process = subprocess.Popen([binary, str(listener.getsockname()[1])])
    connection = None
    try:
        connection, _ = listener.accept()
        connection.settimeout(3)

        def exact(count):
            data = b""
            while len(data) < count:
                part = connection.recv(count - len(data))
                if not part:
                    raise EOFError("Client closed before the test completed")
                data += part
            return data

        request = b""
        while not request.endswith(b"\r\n\r\n"):
            request += exact(1)
            assert len(request) < 8192
        assert request.startswith(b"GET /sendspin HTTP/1.1\r\n")
        headers = {
            line.split(b":", 1)[0].lower(): line.split(b":", 1)[1].strip()
            for line in request.split(b"\r\n")[1:] if b":" in line
        }
        key = headers[b"sec-websocket-key"]
        assert len(base64.b64decode(key)) == 16
        accept = base64.b64encode(hashlib.sha1(
            key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
        connection.sendall(
            b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")

        def receive():
            first, second = exact(2)
            assert second & 128, "Every client WebSocket frame must be masked"
            length = second & 127
            if length == 126:
                length = struct.unpack(">H", exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", exact(8))[0]
            assert length < 65536
            mask = exact(4)
            data = exact(length)
            return first & 15, bytes(value ^ mask[i % 4] for i, value in enumerate(data))

        def frame(opcode, payload, final=True):
            length = len(payload)
            header = bytes([(128 if final else 0) | opcode])
            if length < 126:
                header += bytes([length])
            elif length < 65536:
                header += bytes([126]) + struct.pack(">H", length)
            else:
                header += bytes([127]) + struct.pack(">Q", length)
            connection.sendall(header + payload)

        def message(kind, payload):
            frame(1, json.dumps({"type": kind, "payload": payload}).encode())

        opcode, data = receive()
        assert opcode == 1
        hello = json.loads(data)
        assert hello["type"] == "client/hello"
        assert hello["payload"]["player@v1_support"]["supported_formats"] == [
            {"codec": "pcm", "channels": 2, "sample_rate": 48000, "bit_depth": 16}]
        message("server/hello", {"name": "Mock"})
        started = cleared = got_next = pong = False
        begin = time.monotonic()
        while time.monotonic() - begin < 1.8:
            opcode = 0
            if select.select([connection], [], [], 0.02)[0]:
                opcode, data = receive()
            if opcode == 1:
                event = json.loads(data)
                if event["type"] == "client/time":
                    now = int(time.monotonic() * 1e6)
                    message("server/time", {
                        "client_transmitted": event["payload"]["client_transmitted"],
                        "server_received": now, "server_transmitted": now})
                if event["type"] == "client/command":
                    assert event["payload"]["controller"]["command"] == "next"
                    got_next = True
            elif opcode == 10:
                assert data == b"test"
                pong = True
            if not started and time.monotonic() - begin > 0.1:
                message("stream/start", {"player": {
                    "codec": "pcm", "sample_rate": 48000, "channels": 2, "bit_depth": 16}})
                data = json.dumps({"type": "server/state", "payload": {"metadata": {
                    "title": "Protocol test 🎵", "artist": "Afterglow"}}}).encode()
                # Control frames may interrupt a fragmented JSON message.
                frame(1, data[:20], False)
                frame(9, b"test")
                frame(0, data[20:])
                when = int(time.monotonic() * 1e6) + 200000
                # Extended-length WS frames must split into bounded PCM slots.
                frame(2, b"\x04" + struct.pack(">q", when)
                      + struct.pack("<hh", 10000, 10000) * 48000)
                started = True
            if not cleared and time.monotonic() - begin > 0.65:
                message("stream/clear", {"roles": ["player"]})
                when = int(time.monotonic() * 1e6) + 100000
                frame(2, b"\x04" + struct.pack(">q", when)
                      + struct.pack("<hh", -20000, -20000) * 48000)
                cleared = True
        frame(8, b"\x03\xe8")
        connection.close()
        connection = None
        assert process.wait(timeout=4) == 0, "Native client assertions failed"
        assert got_next and pong
        print("PASS: handshake, masked frames, PCM, timestamp sync, seek/clear, "
              "metadata, fragmented frames, ping/pong, controller command and bounded stop")
    finally:
        if connection is not None:
            connection.close()
        listener.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="Path to the compiled sendspin_client_smoke executable")
    exercise(parser.parse_args().binary)
