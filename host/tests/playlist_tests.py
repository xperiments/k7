#!/usr/bin/env python3
"""Playlists over HTTP: the image carries them, and the broadcast walks one.

The lists live in the Snapshot (ADR 0051), so this starts a host server whose "partition"
file already holds a version 3 image, then asks the routes the page uses. The walk itself
is the broadcast's business; what is proven here is that the choice is applied, that a
pick outside the list ends it, and that every refusal is a refusal.

Usage: playlist_tests.py SERVER FIXTURES_DIR
"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

from snapshot_image import build_image

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


def check_true(label, value):
    check(label, bool(value), True)


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def request(port, method, path, body=None, content_type="application/json"):
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    head = f"{method} {path} HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n"
    if body is not None:
        head += f"Content-Type: {content_type}\r\nContent-Length: {len(body)}\r\n"
    head += "\r\n"
    s.sendall(head.encode() + (body or b""))
    data = b""
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        data += chunk
    s.close()
    raw_head, _, payload = data.partition(b"\r\n\r\n")
    status = int(raw_head.split(b" ")[1]) if raw_head else 0
    try:
        doc = json.loads(payload) if payload else None
    except json.JSONDecodeError:
        doc = None
    return status, doc


def start(snapshot_path, port):
    env = dict(os.environ)
    env["XTAPE_SNAPSHOT"] = snapshot_path
    proc = subprocess.Popen(
        [SERVER, "--music-dir", FIXTURES, "--port", str(port)],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    deadline = time.time() + 20
    while time.time() < deadline:
        try:
            status, _ = request(port, "GET", "/api/status")
            if status == 200:
                return proc
        except OSError:
            pass
        time.sleep(0.1)
    proc.kill()
    raise SystemExit("the server did not come up:\n" + proc.stdout.read().decode(errors="replace"))


def rows():
    return [
        {"path": "a/one.flac", "size": 100, "bpm_x10": 1200, "key": 1, "energy": 70,
         "dance": 60, "valence": 50, "arousal": 40, "mood": 1, "conf": 90, "src": 1,
         "title": "One", "artist": "A", "album": "A1", "genre": "rock"},
    ]


# The lists name files the fixtures directory really holds.
LATENIGHT = {
    "id": 0xAAAA000000000001,
    "name": "Late night",
    "created": 1767225600,
    "rows": ["tone-44100.flac", "tone-32000.mp3"],
}
EMPTY = {"id": 0xCCCC000000000003, "name": "Empty", "created": 1767225600, "rows": []}


def main():
    work = tempfile.mkdtemp(prefix="xtpl")
    part = os.path.join(work, "snapshot.bin")
    with open(part, "wb") as f:
        f.write(build_image(rows(), [LATENIGHT, EMPTY]))
    port = free_port()
    server = start(part, port)
    try:
        status, doc = request(port, "GET", "/api/playlists")
        check("playlists answers", status, 200)
        check("two lists", len(doc["lists"]), 2)
        check("nothing selected yet", doc["selected"], None)
        check("and nothing walking", doc["walking"], False)
        names = [l["name"] for l in doc["lists"]]
        check("names", names, ["Late night", "Empty"])
        check("row count", doc["lists"][0]["rows"], 2)
        check("created", doc["lists"][0]["created"], 1767225600)

        latelight_hex = f"{LATENIGHT['id']:016x}"
        status, doc = request(port, "GET", f"/api/playlists/{latelight_hex}")
        check("one list answers", status, 200)
        check("its rows are paths", doc["rows"], LATENIGHT["rows"])
        check("its name", doc["name"], "Late night")

        status, _ = request(port, "GET", "/api/playlists/00000000000000ff")
        check("an unknown list is 404", status, 404)

        status, doc = request(
            port, "POST", "/api/playlists/select",
            json.dumps({"id": latelight_hex}).encode(),
        )
        check("select answers", status, 200)
        check("and says which order", doc["order"], "list")
        check("and which list", doc["selected"], latelight_hex)

        status, doc = request(port, "GET", "/api/status")
        check("status says the order", doc["order"], "list")
        check("status names the list", doc["playlist"]["name"], "Late night")
        check("status carries its id", doc["playlist"]["id"], latelight_hex)

        status, doc = request(port, "GET", "/api/playlists")
        check("the page sees the selection", doc["selected"], latelight_hex)
        check("and the walk", doc["walking"], True)

        # A pick outside the list ends it: the rule the app relies on.
        outside = None
        for name in sorted(os.listdir(FIXTURES)):
            if name.endswith((".flac", ".mp3", ".wav")) and name not in LATENIGHT["rows"]:
                outside = name
                break
        check_true("a fixture outside the list exists", outside)
        if outside:
            status, _ = request(port, "POST", "/api/play",
                                json.dumps({"path": outside}).encode())
            check("play answers", status, 200)
            status, doc = request(port, "GET", "/api/status")
            check("the pick dropped the list", doc["order"], "tree")
            check("and forgot it", doc["playlist"], None)

        status, _ = request(
            port, "POST", "/api/playlists/select",
            json.dumps({"id": f"{EMPTY['id']:016x}"}).encode(),
        )
        check("an empty list is 409", status, 409)

        status, _ = request(port, "POST", "/api/playlists/select",
                            json.dumps({"id": "00000000000000ff"}).encode())
        check("an unknown id is 404", status, 404)

        status, doc = request(port, "POST", "/api/playlists/select",
                              json.dumps({"id": None}).encode())
        check("clearing is 200", status, 200)
        check("and says the tree order", doc["order"], "tree")

        status, _ = request(port, "POST", "/api/playlists/select", b"{not json")
        check("a bad body is 400", status, 400)

        status, _ = request(port, "POST", "/api/playlists/select", b"{}", "text/plain")
        check("a body that is not JSON is 415", status, 415)
    finally:
        server.kill()
        server.wait()
        os.remove(part)
        os.rmdir(work)

    # An image with no lists at all: the routes say so rather than fail.
    work = tempfile.mkdtemp(prefix="xtpl1")
    part = os.path.join(work, "snapshot.bin")
    with open(part, "wb") as f:
        f.write(build_image(rows()))
    port = free_port()
    server = start(part, port)
    try:
        status, doc = request(port, "GET", "/api/playlists")
        check("an image with no lists answers", status, 200)
        check("with no lists", doc["lists"], [])
        status, _ = request(port, "POST", "/api/playlists/select",
                            json.dumps({"id": f"{LATENIGHT['id']:016x}"}).encode())
        check("and selecting one is 404", status, 404)
        status, doc = request(port, "GET", "/api/status")
        check("the order is the tree", doc["order"], "tree")
    finally:
        server.kill()
        server.wait()
        os.remove(part)
        os.rmdir(work)

    print(f"playlists: {'ok' if FAIL == 0 else 'FAILED'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
