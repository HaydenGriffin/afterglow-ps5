#!/usr/bin/env python3
# Afterglow - Dev requests to a running Afterglow over FTP: status, screenshot, quit, press.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sends dev requests to the running Afterglow title and fetches the answers.

usage: PS5_HOST=<address> AFTERGLOW_DEV_TOKEN=<token> tools/afterglow-dev.py <request> [out dir]

  status             write dev/status.json and download it
  screenshot <name>  picture the next frame as dev/<name>.bmp and download it
  quit               close the app the clean way (as holding Circle does)
  press <button>...  tap buttons in turn, one per request: up, down, left, right,
                     cross, circle, triangle, square, l1, r1, l2, r2, options,
                     touchpad, l3, r3

The title must be running and its afterglow.conf must have the same
dev_requests=<token> line (16 to 128 letters, digits, '-', '_' or '.').
The request goes to /data/homebrew/<TITLE_ID>/dev/request.txt (the app's
/app0/dev/request.txt, polled once a second). The answer is read from the
title's storage, /mnt/sandbox/<TITLE_ID>_000/download0/hui/dev, which exists
only while the title runs.

The script never launches, kills or uploads the app. The only file it
writes or deletes on the console is its own request file.

Environment:
  PS5_HOST             console address (required)
  AFTERGLOW_DEV_TOKEN  the dev_requests token (required)
  FTP_PORT             default 2121
  DEV_TIMEOUT          seconds to wait for the answer (default 20)
"""

import importlib.util
import json
import os
import re
import secrets
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAME = re.compile(r"[A-Za-z0-9_-]{1,48}")
TOKEN = re.compile(r"[A-Za-z0-9._-]{16,128}")
BUTTONS = ("up", "down", "left", "right", "cross", "circle", "triangle", "square", "l1", "r1",
           "l2", "r2", "options", "touchpad", "l3", "r3")


def load_tour():
    """console-tour.py's FTP session (verified writes, sandbox listing)."""
    spec = importlib.util.spec_from_file_location("console_tour", ROOT / "tools/console-tour.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def request_line(verb, argument, token, request_id):
    return f"{verb} {argument} {token} {request_id}\n"


def parse_answer(text, request_id):
    """(ok, detail) when dev/result.txt answers request_id, else None."""
    parts = text.strip().split(" ", 2)
    if len(parts) < 2 or parts[0] != request_id:
        return None
    return parts[1] == "ok", parts[2] if len(parts) > 2 else ""


def send(console, request_path, dev, line, request_id, timeout):
    """Writes one request and waits for its answer: (ok, detail) or None."""
    console.write(request_path, line.encode())
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data = console.read(f"{dev}/result.txt")
        answer = parse_answer(data.decode(errors="replace"), request_id) if data else None
        if answer is not None:
            return answer
        time.sleep(0.5)
    return None


def main():
    args = sys.argv[1:]
    if not args or args[0] not in ("status", "screenshot", "quit", "press"):
        raise SystemExit(__doc__)
    verb = args[0]
    arguments = ["-"]
    if verb == "screenshot":
        if len(args) < 2 or not NAME.fullmatch(args[1]):
            raise SystemExit("screenshot needs a name: 1 to 48 letters, digits, '-' or '_'")
        arguments = [args[1]]
        args = args[2:]
    elif verb == "press":
        arguments = args[1:]
        if not arguments or any(button not in BUTTONS for button in arguments):
            raise SystemExit("press needs buttons: " + ", ".join(BUTTONS))
        args = []
    else:
        args = args[1:]
    out = Path(args[0]) if args else Path(".")

    tour = load_tour()
    host = os.environ.get("PS5_HOST") or tour.stop("set PS5_HOST")
    token = os.environ.get("AFTERGLOW_DEV_TOKEN", "")
    if not TOKEN.fullmatch(token):
        tour.stop("set AFTERGLOW_DEV_TOKEN to the dev_requests value in afterglow.conf")
    port = int(os.environ.get("FTP_PORT", "2121"))
    timeout = float(os.environ.get("DEV_TIMEOUT", "20"))
    title = json.loads((ROOT / "sce_sys/param.json").read_text())["titleId"]
    request_path = f"/data/homebrew/{title}/dev/request.txt"
    dev = f"/mnt/sandbox/{title}_000/download0/hui/dev"

    console = tour.Console(host, port)
    answer = None
    fetched = None
    try:
        if title not in tour.running_titles(console):
            tour.stop(f"{title} is not running; launch it first (this tool never launches)")
        for argument in arguments:
            request_id = f"r{int(time.time())}{secrets.token_hex(3)}"
            tour.say(f"Sending {verb} {argument if argument != '-' else ''}".rstrip())
            answer = send(console, request_path, dev,
                          request_line(verb, argument, token, request_id), request_id, timeout)
            if answer is None or not answer[0]:
                break
        if answer and answer[0] and verb in ("status", "screenshot"):
            name = "status.json" if verb == "status" else f"{arguments[0]}.bmp"
            fetched = console.read(f"{dev}/{name}")
            if fetched is not None:
                out.mkdir(parents=True, exist_ok=True)
                (out / name).write_bytes(fetched)
        try:
            console.ftp.sendcmd(f"DELE {request_path}")  # only our own request
        except tour.all_errors:
            pass
    finally:
        console.close()

    if answer is None:
        tour.stop(f"no answer in {timeout:.0f} s (is dev_requests set to this token?)")
    if not answer[0]:
        tour.stop(f"the app answered: {answer[1]}")
    if verb == "status":
        if fetched is None:
            tour.stop("status.json could not be downloaded")
        print(fetched.decode(errors="replace"), end="")
    elif verb == "screenshot":
        if fetched is None:
            tour.stop(f"{arguments[0]}.bmp could not be downloaded")
        tour.say(f"Saved {out / (arguments[0] + '.bmp')} ({len(fetched)} bytes)")
    elif verb == "press":
        tour.say(f"Pressed {' '.join(arguments)}")
    else:
        tour.say("The app is closing itself")


if __name__ == "__main__":
    main()
