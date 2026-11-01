#!/usr/bin/env python3
"""The front door, end to end, against the host server.

Builds a small page tree, points the server at it with XTAPE_SHELL, and checks the
things the page and its service worker depend on: the cache headers, the content
types, the fallback to index.html for the app's own routes, `/api/status`, and the
index still answering at /library.

Usage: front_door_tests.py SERVER FIXTURES_DIR
"""
import gzip
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

SERVER = sys.argv[1]
FIXTURES = sys.argv[2]
FAIL = 0


def check(label, got, want):
    global FAIL
    if got != want:
        print(f"FAIL {label}: got {got!r}, want {want!r}")
        FAIL += 1
    else:
        print(f"  ok    {label}")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def request(port, path, accept=None):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    headers_out = f"GET {path} HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n"
    if accept:
        headers_out += f"Accept-Encoding: {accept}\r\n"
    s.sendall((headers_out + "\r\n").encode())
    data = b""
    while True:
        part = s.recv(65536)
        if not part:
            break
        data += part
    s.close()
    head, _, body = data.partition(b"\r\n\r\n")
    lines = head.decode("latin1").split("\r\n")
    status = int(lines[0].split(" ")[1])
    headers = {}
    for line in lines[1:]:
        name, _, value = line.partition(":")
        headers[name.strip().lower()] = value.strip()
    return status, headers, head.decode("latin1"), body


def get(port, path):
    status, headers, head, body = request(port, path)
    return status, headers, head, body.decode("utf-8", "replace")


def start(web_root, music, port):
    env = dict(os.environ)
    if web_root is None:
        env.pop("XTAPE_SHELL", None)
    else:
        env["XTAPE_SHELL"] = web_root
    proc = subprocess.Popen(
        [SERVER, "--music-dir", music, "--port", str(port)],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.sendall(b"GET /library HTTP/1.0\r\nHost: t\r\n\r\n")
            s.recv(64)
            s.close()
            return proc
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise SystemExit("the server did not come up:\n" + proc.stdout.read().decode(errors="replace"))


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def main():
    tmp = tempfile.mkdtemp(prefix="xtape-frontdoor-")
    dist = os.path.join(tmp, "dist")
    write(os.path.join(dist, "index.html"), "<!doctype html><title>xtape</title>")
    write(os.path.join(dist, "assets", "index-abc123.js"), "console.log('app');")
    write(os.path.join(dist, "sw.js"), "self.addEventListener('fetch', () => {});")
    write(os.path.join(dist, "manifest.webmanifest"), '{"name":"xtape"}')

    port = free_port()
    proc = start(dist, FIXTURES, port)
    try:
        status, headers, _, body = get(port, "/")
        check("the page is at /", status, 200)
        check("it is html", headers.get("content-type"), "text/html; charset=utf-8")
        check("it is revalidated every time", headers.get("cache-control"), "no-cache")
        check("and it is the index", body, "<!doctype html><title>xtape</title>")

        status, headers, head, _ = get(port, "/assets/index-abc123.js")
        check("a hashed asset is served", status, 200)
        check("as javascript", headers.get("content-type"), "text/javascript; charset=utf-8")
        check("and never revalidated", headers.get("cache-control"),
              "public, max-age=31536000, immutable")
        check("one Cache-Control, not two", head.lower().count("cache-control:"), 1)

        # The Shell is packed compressed: the build writes `x.js.gz` and drops the original,
        # so the server sends the stored bytes and says so, and a client that cannot take
        # gzip is told rather than handed bytes it will not decode.
        with open(os.path.join(dist, "assets", "packed-def456.js.gz"), "wb") as f:
            f.write(gzip.compress(b"console.log('packed');"))

        status, headers, head, raw = request(port, "/assets/packed-def456.js", accept="gzip, deflate")
        check("a compressed-only file is served to a gzip client", status, 200)
        check("with the encoding header", headers.get("content-encoding"), "gzip")
        check("under the plain name's content type", headers.get("content-type"),
              "text/javascript; charset=utf-8")
        check("and a length that matches the bytes sent", int(headers.get("content-length", 0)),
              len(raw))
        check("one Cache-Control, not two", head.lower().count("cache-control:"), 1)
        check("a cache is told the encoding varies", headers.get("vary"), "Accept-Encoding")
        check("and the bytes inflate to the file", gzip.decompress(raw),
              b"console.log('packed');")

        status, _, _, _ = request(port, "/assets/packed-def456.js")
        check("a client without gzip is refused, not 404d", status, 406)

        # The page itself stays plain, so `/` answers any client.
        status, headers, _, _ = request(port, "/")
        check("the page is not compressed", headers.get("content-encoding"), None)

        status, headers, _, _ = get(port, "/sw.js")
        check("the worker is served", status, 200)
        check("and is revalidated every time", headers.get("cache-control"), "no-cache")

        status, _, _, _ = get(port, "/player")
        check("an unknown path is a 404, not the page", status, 404)

        status, headers, _, body = get(port, "/api/status")
        check("status is a 200", status, 200)
        check("it is json", headers.get("content-type"), "application/json")
        check("and is never cached", headers.get("cache-control"), "no-store")
        status_doc = json.loads(body)
        check("it reports the card the host always has", status_doc.get("card"), True)
        check("it counts the fixture tracks", status_doc.get("tracks", 0) > 0, True)
        check("it reports what is playing", "playing" in status_doc, True)
        status2, _, _, _ = get(port, "/api/statuses")
        check("a longer path is not the status route", status2, 404)

        status, _, _, _ = get(port, "/assets/missing.js")
        check("a missing asset is a 404", status, 404)

        status, _, _, _ = get(port, "/api/nope")
        check("an unregistered API path is a 404, not the page", status, 404)

        status, _, _, _ = get(port, "/library")
        check("the index is still at /library", status, 200)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    # A Shell directory that exists but has no index.html is a device fault, not a typo.
    broken = os.path.join(tmp, "broken")
    write(os.path.join(broken, "assets", "x.js"), "//")
    port = free_port()
    proc = start(broken, FIXTURES, port)
    try:
        status, _, _, _ = get(port, "/")
        check("a packed page that is not there is a 503", status, 503)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    # A firmware with no page: `/` says so, and everything else keeps working.
    port = free_port()
    proc = start(None, FIXTURES, port)
    try:
        status, _, _, body = get(port, "/")
        check("no page is a 503", status, 503)
        check("and says where the index is", "/library" in body, True)
        status, _, _, _ = get(port, "/library")
        check("while the index still answers", status, 200)
        status, _, _, _ = get(port, "/nope")
        check("and an unknown path is still a 404", status, 404)
        status, _, _, _ = get(port, "/api/status")
        check("and status still answers", status, 200)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"front door: {'FAILED' if FAIL else 'ok'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
