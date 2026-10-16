#!/usr/bin/env python3
# Afterglow - Disposable Home Assistant websocket fixture; never contacts a real house.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later

import base64
import binascii
import json
import select
import socket
import struct
import subprocess
import sys
import time

PONGS = []
ENTITIES = ["sensor.living_room_temperature", "light.living_room_lights", "scene.broken"]


def exact(connection, count):
    data = b""
    while len(data) < count:
        part = connection.recv(count - len(data))
        if not part:
            raise EOFError
        data += part
    return data


def frame(opcode, data, fin=True):
    head = bytes([(0x80 if fin else 0) | opcode])
    if len(data) < 126:
        head += bytes([len(data)])
    elif len(data) < 65536:
        head += b"\x7e" + struct.pack(">H", len(data))
    else:
        head += b"\x7f" + struct.pack(">Q", len(data))
    return head + data


def send(connection, message):
    connection.sendall(frame(1, json.dumps(message).encode()))


def send_fragmented(connection, message):
    data = json.dumps(message).encode()
    half = len(data) // 2
    # A ping between the fragments, as servers may interleave control frames.
    connection.sendall(frame(1, data[:half], fin=False) + frame(9, b"hi") + frame(0, data[half:]))


def receive(connection):
    """Returns (opcode, payload); client frames must be masked."""
    first, second = exact(connection, 2)
    assert second & 128, "client frames must be masked"
    length = second & 127
    if length == 126:
        length = struct.unpack(">H", exact(connection, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", exact(connection, 8))[0]
    mask = exact(connection, 4)
    data = bytes(value ^ mask[i % 4] for i, value in enumerate(exact(connection, length)))
    return first & 15, data


def receive_json(connection):
    while True:
        opcode, data = receive(connection)
        if opcode == 10:
            PONGS.append(data)
            continue
        assert opcode == 1, "unexpected opcode %d" % opcode
        return json.loads(data)


def accept(listener):
    connection, _ = listener.accept()
    connection.settimeout(3)
    request = b""
    while not request.endswith(b"\r\n\r\n"):
        request += exact(connection, 1)
        assert len(request) < 8192
    assert request.startswith(b"GET /api/websocket HTTP/1.1\r\n"), request
    # aiohttp, which serves Home Assistant, refuses keys that are not 16 bytes in base64.
    key = next(line.split(b":", 1)[1].strip() for line in request.split(b"\r\n")
               if line.lower().startswith(b"sec-websocket-key:"))
    try:
        assert len(base64.b64decode(key, validate=True)) == 16, key
    except binascii.Error as error:
        raise AssertionError(key) from error
    connection.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                       b"Connection: Upgrade\r\nSec-WebSocket-Accept: x\r\n\r\n")
    return connection


def states(light):
    return {"a": {
        "sensor.living_room_temperature": {"s": "19.46", "a": {"unit_of_measurement": "°C",
                                           "friendly_name": "Living Room Temperature"}, "c": "1", "lc": 1},
        "light.living_room_lights": {"s": light, "a": {"friendly_name": "Living Room Lights"},
                                     "c": "2", "lc": 1},
        "scene.broken": {"s": "2026-10-15T08:00:00+00:00", "a": {"friendly_name": "Broken"}, "c": "3", "lc": 1},
    }}


def session(connection, number, token_ok):
    """One connection. Returns when the fixture drops it or the client leaves."""
    send(connection, {"type": "auth_required", "ha_version": "2026.10.0"})
    auth = receive_json(connection)
    assert auth == {"type": "auth", "access_token": "fixture-token" if token_ok else "wrong-token"}, auth
    if not token_ok:
        send(connection, {"type": "auth_invalid", "message": "Invalid access token or password"})
        return
    send(connection, {"type": "auth_ok", "ha_version": "2026.10.0"})
    subscribe = receive_json(connection)
    assert subscribe["type"] == "subscribe_entities", subscribe
    assert subscribe["entity_ids"] == ENTITIES, subscribe
    sub = subscribe["id"]
    send(connection, {"id": sub, "type": "result", "success": True, "result": None})
    light = "off" if number == 1 else "on"
    send_fragmented(connection, {"id": sub, "type": "event", "event": states(light)})
    last_id = sub
    while True:
        try:
            message = receive_json(connection)
        except (EOFError, ConnectionError, socket.timeout):
            return
        assert message["id"] > last_id, "ids must increase"
        last_id = message["id"]
        if message["type"] == "ping":
            send(connection, {"id": message["id"], "type": "pong"})
            continue
        assert message["type"] == "call_service", message
        assert message["domain"] != "sensor", "read-only entities never reach Home Assistant"
        target = message["target"]["entity_id"]
        if target == "light.living_room_lights":
            assert (message["domain"], message["service"]) == ("light", "toggle"), message
            send(connection, {"id": message["id"], "type": "result", "success": True,
                              "result": {"context": {"id": "x"}}})
            send(connection, {"id": sub, "type": "event",
                              "event": {"c": {target: {"+": {"s": "on", "lc": 2}}}}})
        elif target == "scene.broken":
            assert (message["domain"], message["service"]) == ("scene", "turn_on"), message
            send(connection, {"id": message["id"], "type": "result", "success": False,
                              "error": {"code": "home_assistant_error", "message": "Scene exploded"}})
            # Give the client time to send (and refuse locally) the sensor press, then drop.
            time.sleep(0.3)
            return
        else:
            raise AssertionError("unexpected target " + target)


def exercise(binary, mode):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    process = subprocess.Popen([binary, str(listener.getsockname()[1]), mode])
    connections = 0
    started = time.monotonic()
    try:
        while process.poll() is None and time.monotonic() - started < 20:
            if not select.select([listener], [], [], .1)[0]:
                continue
            connection = accept(listener)
            connections += 1
            try:
                session(connection, connections, mode == "ok")
                if mode == "ok" and connections >= 2:
                    # Keep the second session open until the client stops.
                    while process.poll() is None:
                        if select.select([connection], [], [], .1)[0]:
                            try:
                                opcode, data = receive(connection)
                            except EOFError:
                                break
                            if opcode == 1 and json.loads(data).get("type") == "ping":
                                send(connection, {"id": json.loads(data)["id"], "type": "pong"})
            finally:
                connection.close()
        code = process.wait(timeout=5)
        assert code == 0, "client exited %d" % code
        if mode == "ok":
            assert connections == 2, connections
            assert b"hi" in PONGS, "the interleaved ping must be answered"
        else:
            assert connections == 1, "a refused token must not be retried (%d)" % connections
    finally:
        listener.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


if __name__ == "__main__":
    binary = sys.argv[1]
    exercise(binary, "ok")
    print("PASS: auth, bounded subscribe, fragmented event with ping, toggle + state change, "
          "refused call, local refusal of read-only entity, reconnect after drop")
    exercise(binary, "invalid")
    print("PASS: invalid token reported once, presses refused, no reconnect, bounded stop")
