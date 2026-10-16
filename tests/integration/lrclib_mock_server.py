#!/usr/bin/env python3
# Afterglow - A disposable HTTPS stand-in for lrclib.net's search, on loopback.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: lrclib_mock_server.py CERT KEY PORT_FILE
# Serves until killed. Writes the port it listens on to PORT_FILE.

import http.server
import json
import ssl
import sys

RESULTS = [
    {
        "trackName": "That's It (feat. Future & Metro Boomin)",
        "artistName": "Yung Lean",
        "albumName": "Illustrative Album",
        "duration": 162.0,
        "instrumental": False,
        "plainLyrics": "An illustrative line",
        "syncedLyrics": "[00:18.88]An illustrative line",
    },
    {
        "trackName": "That's It",
        "artistName": "Someone Else",
        "duration": 162.0,
        "instrumental": False,
        "plainLyrics": "Not this one",
        "syncedLyrics": None,
    },
]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_GET(self):
        agent = self.headers.get("User-Agent", "")
        if not agent.startswith("Afterglow PS5/") or "github.com/HaydenGriffin/afterglow-ps5" not in agent:
            self.send_response(400)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        body = json.dumps(RESULTS).encode()
        if self.path.startswith("/chunked"):
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for start in range(0, len(body), 100):
                piece = body[start:start + 100]
                self.wfile.write(b"%x\r\n%s\r\n" % (len(piece), piece))
            self.wfile.write(b"0\r\n\r\n")
            return
        if self.path.startswith("/limited"):
            self.send_response(429)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if not self.path.startswith("/api/search?track_name=that's%20it&artist_name=Yung%20Lean"):
            body = b"[]"
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    cert, key, port_file = sys.argv[1:4]
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    with open(port_file, "w", encoding="utf-8") as out:
        out.write(str(server.server_address[1]))
    server.serve_forever()


if __name__ == "__main__":
    main()
