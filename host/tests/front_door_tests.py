#!/usr/bin/env python3
"""The device's HTTP surface without a page, end to end, against the host server.

The product is Chrome-only and the App is hosted, so the device serves no page: `/` and
`/bridge` are 404s like any unknown path, while `/library`, `/stream` and `/api/*` are
the device's own. This checks that shape and `/api/status`, the one route front_door
registers.

Usage: front_door_tests.py SERVER FIXTURES_DIR
"""
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


def request(port, path):
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    headers_out = f"GET {path} HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n"
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


def start(music, port):
    proc = subprocess.Popen(
        [SERVER, "--music-dir", music, "--port", str(port)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
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


def stop(proc):
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def main():
    port = free_port()
    proc = start(FIXTURES, port)
    try:
        # No page, but a doorbell: the names a browser types or a bookmark holds are sent
        # to the App (ADR 0067), and nothing else gains a route.
        for path in ("/", "/bridge", "/bridge/", "/index.html", "/app", "/app/"):
            status, headers, _, _ = get(port, path)
            check(f"{path} redirects", status, 302)
            check(f"{path} names the App", headers.get("location"),
                  "https://k7.xperiments.dev/#/player")
        for path in ("/nope", "/apps", "/index.htm"):
            status, headers, _, body = get(port, path)
            check(f"{path} is a 404", status, 404)
            check(f"{path} says no such path", "no such path" in body, True)
        status, headers, _, body = get(port, "/nope")
        check("the 404 is the server's error page", headers.get("content-type"),
              "text/html; charset=utf-8")
        check("and it is not compressed", headers.get("content-encoding"), None)

        # The device's own surfaces.
        status, headers, _, body = get(port, "/api/status")
        check("status is a 200", status, 200)
        check("it is json", headers.get("content-type"), "application/json")
        check("and is never cached", headers.get("cache-control"), "no-store")
        status_doc = json.loads(body)
        check("it reports the card the host always has", status_doc.get("card"), True)
        check("an unmarked card reports no mark", status_doc.get("mark"), None)
        check("it counts the fixture tracks", status_doc.get("tracks", 0) > 0, True)
        check("it reports what is playing", "playing" in status_doc, True)

        status, _, _, _ = get(port, "/api/statuses")
        check("a longer path is not the status route", status, 404)
        status, _, _, _ = get(port, "/api/nope")
        check("an unregistered API path is a 404", status, 404)
        status, _, _, _ = get(port, "/nope")
        check("an unknown path is a 404", status, 404)

        status, headers, _, body = get(port, "/library")
        check("the index is at /library", status, 200)
        check("and is html", headers.get("content-type"), "text/html; charset=utf-8")

        status, headers, _, _ = get(port, "/api/library.bin")
        check("the library view answers", status, 200)
        check("as the binary view", headers.get("content-type"), "application/octet-stream")
    finally:
        stop(proc)

    # The card's own name, read from the mark beside the Music directory. A marked card gets
    # its id in the status; it is a name, not a credential, and the route only reports it.
    mark_id = "f0b1c2d3-e4f5-4a6b-8c9d-0e1f2a3b4c5d"
    root = tempfile.mkdtemp(prefix="k7-mark-")
    try:
        shutil.copytree(os.path.abspath(FIXTURES), os.path.join(root, "music"))
        with open(os.path.join(root, "k7.card"), "w", encoding="utf-8") as fh:
            json.dump({"v": 1, "id": mark_id, "created": "2026-11-06T22:00:00Z"}, fh)
        marked_port = free_port()
        marked = start(os.path.join(root, "music"), marked_port)
        try:
            status_doc = json.loads(get(marked_port, "/api/status")[3])
            check("the status names the card's mark", status_doc.get("mark"), mark_id)
        finally:
            stop(marked)
    finally:
        shutil.rmtree(root, ignore_errors=True)

    print(f"front door: {'FAILED' if FAIL else 'ok'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
