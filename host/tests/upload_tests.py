#!/usr/bin/env python3
"""The snapshot arrives over HTTP, is validated, and is written whole or not at all.

The browser analyses the card where the card is - in the computer - and posts the
image to the device. On the host the "partition" is the file XTAPE_SNAPSHOT names, so
the same seam is exercised: a good image lands byte for byte, a corrupted one leaves
the partition untouched, and a body the route did not ask for is refused.

Usage: upload_tests.py SERVER FIXTURES_DIR
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

from snapshot_image import build_image

SERVER = sys.argv[1]
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


def rows():
    return [
        {"path": "a/one.flac", "size": 100, "bpm_x10": 1200, "key": 1, "energy": 70,
         "dance": 60, "valence": 50, "arousal": 40, "mood": 1, "conf": 90, "src": 1,
         "title": "One", "artist": "A", "album": "A1", "genre": "rock"},
        {"path": "a/two.flac", "size": 200, "bpm_x10": 900, "key": 2, "energy": 30,
         "dance": 40, "valence": 60, "arousal": 20, "mood": 8, "conf": 80, "src": 1,
         "title": "Two", "artist": "B", "album": "B1", "genre": "jazz"},
        {"path": "a/three.flac", "size": 300, "bpm_x10": 1300, "key": 3, "energy": 90,
         "dance": 80, "valence": 40, "arousal": 70, "mood": 4, "conf": 70, "src": 1,
         "title": "Three", "artist": "C", "album": "C1", "genre": "techno"},
    ]


def start(snapshot_path, music, port):
    env = dict(os.environ)
    env["XTAPE_SNAPSHOT"] = snapshot_path
    proc = subprocess.Popen(
        [SERVER, "--music-dir", music, "--port", str(port)],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.sendall(b"GET /api/library HTTP/1.0\r\nHost: t\r\n\r\n")
            s.recv(64)
            s.close()
            return proc
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise SystemExit("the server did not come up:\n" + proc.stdout.read().decode(errors="replace"))


def post(port, path, body, content_type="application/octet-stream"):
    s = socket.create_connection(("127.0.0.1", port), timeout=15)
    head = (f"POST {path} HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n"
            f"Content-Type: {content_type}\r\nContent-Length: {len(body)}\r\n\r\n").encode()
    s.sendall(head + body)
    data = b""
    while True:
        part = s.recv(65536)
        if not part:
            break
        data += part
    s.close()
    head, _, body = data.partition(b"\r\n\r\n")
    status = int(head.split(b" ")[1])
    return status, body.decode("utf-8", "replace")


def stats(port):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": "library_stats", "arguments": {}}}).encode()
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.sendall(b"POST /mcp HTTP/1.0\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n"
              b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
    data = b""
    while True:
        part = s.recv(65536)
        if not part:
            break
        data += part
    s.close()
    doc = json.loads(data.partition(b"\r\n\r\n")[2])
    return json.loads(doc["result"]["content"][0]["text"])


def stop(proc):
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


def main():
    tmp = tempfile.mkdtemp(prefix="xtape-upload-")
    store = os.path.join(tmp, "snapshot.bin")
    music = os.path.join(tmp, "music")
    os.makedirs(music)
    good = build_image(rows())

    # A device that starts with no snapshot, the way a freshly flashed one does.
    port = free_port()
    proc = start(store, music, port)
    try:
        check("no snapshot yet", stats(port)["snapshot"].get("rows", 0), 0)

        status, body = post(port, "/api/snapshot", good, "application/json")
        check("a JSON body is refused", status, 415)

        corrupt = bytearray(good)
        corrupt[-1] ^= 0xFF
        status, body = post(port, "/api/snapshot", bytes(corrupt))
        check("a corrupt image is refused", status, 400)
        check("and nothing was written", os.path.exists(store), False)

        status, body = post(port, "/api/snapshot", b"", "application/octet-stream")
        check("an empty upload is refused", status, 400)

        status, body = post(port, "/api/snapshot", good)
        check("a valid image is applied", status, 200)
        check("the reply names the bytes", json.loads(body).get("bytes"), len(good))
        with open(store, "rb") as f:
            check("and it is on the partition byte for byte", f.read(), good)
    finally:
        stop(proc)

    # What a reboot sees: the image that was written is the one served.
    port = free_port()
    proc = start(store, music, port)
    try:
        s = stats(port)
        check("the new image is what the device serves", s["snapshot"].get("rows"), 3)
        check("and it opened", s["snapshot"].get("state"), "ok")
    finally:
        stop(proc)

    # A body larger than the route allows is refused before anything else happens.
    port = free_port()
    proc = start(store, music, port)
    try:
        # Only the headers: the server refuses the length before it reads the body, and a
        # client that sends 300 KB anyway races the close.
        s = socket.create_connection(("127.0.0.1", port), timeout=5)
        s.sendall(b"POST /api/snapshot HTTP/1.0\r\nHost: t\r\n"
                  b"Content-Type: application/octet-stream\r\n"
                  b"Content-Length: 307200\r\n\r\n")
        head = b""
        while b"\r\n\r\n" not in head:
            part = s.recv(4096)
            if not part:
                break
            head += part
        s.close()
        check("an oversized upload is a 413", int(head.split(b" ")[1]), 413)
    finally:
        stop(proc)
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"snapshot upload: {'FAILED' if FAIL else 'ok'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
