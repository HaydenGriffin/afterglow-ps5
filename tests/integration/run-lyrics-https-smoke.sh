#!/usr/bin/env bash
# Afterglow - The HTTPS client and online lyrics search against a disposable TLS server.
# Copyright (C) 2026 Afterglow contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makes a throwaway CA and a certificate for localhost, serves an lrclib.net
# stand-in on loopback, and checks that the client finds lyrics through it,
# reads chunked answers (even one byte per chunk) and errors, refuses an answer
# whose chunk framing outgrows the wire allowance, and refuses an untrusted CA and a name the
# certificate does not cover. Nothing leaves the machine unless
# AFTERGLOW_LRCLIB_LIVE=1, which also runs one real search on lrclib.net.
# Needs openssl, Python 3 and a C++20 compiler (CXX, CC); Linux or macOS.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out="$root/build/tests/lyrics-https"
rm -rf -- "$out"
mkdir -p "$out"
HOST_CC=${CC:-${HOST_CC:-clang}} bash "$root/tools/build-bearssl.sh" host >/dev/null

"${CXX:-c++}" -std=c++20 -O1 -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti -pthread \
    -I"$root/src" -I"$root/.deps/bearssl/src/inc" \
    "$root/src/afterglow/https.cpp" "$root/src/afterglow/http_response.cpp" \
    "$root/src/afterglow/lrclib.cpp" "$root/src/afterglow/online_lyrics.cpp" \
    "$root/src/afterglow/lyrics_lookup.cpp" "$root/src/afterglow/lyrics.cpp" \
    "$root/src/core/save_file.cpp" "$root/host/platform_host.cpp" \
    "$root/tests/integration/lyrics_https_smoke.cpp" \
    "$root/.deps/bearssl/host/libbearssl.a" -o "$out/lyrics_https_smoke"

certificates() {
    local name=$1
    openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 2 \
        -subj "/CN=Afterglow test CA $name" -keyout "$out/$name-ca.key" -out "$out/$name-ca.pem" \
        -addext basicConstraints=critical,CA:TRUE -addext keyUsage=critical,keyCertSign,cRLSign \
        2>/dev/null
}
certificates trusted
certificates other
openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -subj /CN=localhost \
    -keyout "$out/server.key" -out "$out/server.csr" 2>/dev/null
printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n' \
    >"$out/server.ext"
openssl x509 -req -in "$out/server.csr" -CA "$out/trusted-ca.pem" -CAkey "$out/trusted-ca.key" \
    -CAcreateserial -days 2 -extfile "$out/server.ext" -out "$out/server.pem" 2>/dev/null

python3 "$root/tests/integration/lrclib_mock_server.py" "$out/server.pem" "$out/server.key" \
    "$out/port" &
server=$!
trap 'kill "$server" 2>/dev/null || true' EXIT
for _ in $(seq 50); do [[ -s $out/port ]] && break; sleep 0.1; done
port=$(<"$out/port")
smoke="$out/lyrics_https_smoke"

SSL_CERT_FILE="$out/trusted-ca.pem" "$smoke" lyrics localhost "$port"
SSL_CERT_FILE="$out/trusted-ca.pem" "$smoke" get localhost "$port" /chunked ok 200
# The console's CA_LIST.cer layout: CRLF lines and a "#" title before each certificate.
for name in other trusted; do
    printf '#Afterglow test CA %s\n' "$name"
    cat "$out/$name-ca.pem"
done | sed 's/$/\r/' >"$out/console-ca-list.cer"
SSL_CERT_FILE="$out/console-ca-list.cer" "$smoke" get localhost "$port" /chunked ok 200
SSL_CERT_FILE="$out/trusted-ca.pem" "$smoke" get localhost "$port" /limited ok 429
# A body trickled one byte per chunk and record is read; one that never ends is
# refused once its framing exceeds the wire allowance, before the body limit.
SSL_CERT_FILE="$out/trusted-ca.pem" "$smoke" get localhost "$port" /tiny-ok ok 200
SSL_CERT_FILE="$out/trusted-ca.pem" "$smoke" get localhost "$port" /tiny-flood fail 0 \
    "answer too large"
# Certificate checks are on: another CA, or a name the certificate lacks, fail.
SSL_CERT_FILE="$out/other-ca.pem" "$smoke" get localhost "$port" /chunked fail
SSL_CERT_FILE="$out/trusted-ca.pem" "$smoke" get 127.0.0.1 "$port" /chunked fail
if [[ ${AFTERGLOW_LRCLIB_LIVE:-0} == 1 ]]; then
    unset SSL_CERT_FILE
    "$smoke" get lrclib.net 443 "/api/search?track_name=that's%20it&artist_name=Yung%20Lean" ok 200
    "$smoke" get expired.badssl.com 443 / fail
    "$smoke" get wrong.host.badssl.com 443 / fail
fi
echo "lyrics https smoke: passed"
