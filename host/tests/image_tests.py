#!/usr/bin/env python3
"""The image the device answers from: GET /api/snapshot.

The App wrote the image when it measured the card, and the device serves the one it is
answering from so a browser that has never met the card can read the names out of it. This
proves the route: the bytes come back exactly, the type and the download name are the App's,
the CORS reply lets the deployed origin read them, and a device with no image says so.

Usage: image_tests.py SERVER FIXTURES_DIR
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from snapshot_image import build_image, fnv1a64  # noqa: E402

SERVER = sys.argv[1]
FIXTURES = Path(sys.argv[2])
FAIL = 0
DEPLOYED = "https://k7.xperiments.dev"

# A real image, built the way the host producer builds one: the route serves the copy the
# reader validated at boot, so the bytes have to be an image the reader accepts.
_ROW = {
    "path": "Sting/Blue/01.flac", "size": 100, "bpm_x10": 1200, "key": 1, "energy": 70,
    "dance": 60, "valence": 50, "arousal": 40, "mood": 1, "conf": 90, "src": 1,
    "title": "Ten Summoner's Tales", "artist": "Sting", "album": "Ten Summoner's Tales",
    "genre": "rock",
}
IMAGE = build_image([_ROW])


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


def request(port, method, path, headers=None):
    headers = dict(headers or {})
    head = f"{method} {path} HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n"
    for name, value in headers.items():
        head += f"{name}: {value}\r\n"
    head += "\r\n"
    s = socket.create_connection(("127.0.0.1", port), timeout=20)
    try:
        s.sendall(head.encode())
    except OSError:
        pass
    data = b""
    while True:
        try:
            chunk = s.recv(65536)
        except OSError:
            break
        if not chunk:
            break
        data += chunk
    s.close()
    head_bytes, _, payload = data.partition(b"\r\n\r\n")
    lines = head_bytes.decode("latin1").split("\r\n")
    status = int(lines[0].split()[1])
    reply = {}
    for line in lines[1:]:
        name, _, value = line.partition(":")
        reply[name.strip().lower()] = value.strip()
    return status, reply, payload


def start(card, port, image):
    env = dict(os.environ)
    if image is not None:
        env["XTAPE_SNAPSHOT"] = str(image)
    else:
        env.pop("XTAPE_SNAPSHOT", None)
    proc = subprocess.Popen(
        [SERVER, "--music-dir", str(card / "music"), "--port", str(port)],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.sendall(b"GET /api/status HTTP/1.0\r\nHost: t\r\n\r\n")
            s.recv(64)
            s.close()
            return proc
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise SystemExit("the server did not come up:\n" + proc.stdout.read().decode(errors="replace"))


def main():
    card = Path(tempfile.mkdtemp(prefix="k7-image-"))
    try:
        (card / "music").mkdir()
        shutil.copy(FIXTURES / "tone-44100.flac", card / "music" / "one.flac")

        image_file = card / "image.k7db"
        image_file.write_bytes(IMAGE)

        port = free_port()
        proc = start(card, port, None)
        try:
            code, reply, body = request(port, "GET", "/api/snapshot", {"Origin": DEPLOYED})
            check("a device with no image is a 404", code, 404)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

        port = free_port()
        proc = start(card, port, image_file)
        try:
            code, reply, body = request(port, "GET", "/api/snapshot", {"Origin": DEPLOYED})
            check("the image is served", code, 200)
            check("as a download with its own name",
                  reply.get("content-disposition"), 'attachment; filename="k7.db"')
            check("as bytes", reply.get("content-type"), "application/octet-stream")
            check("byte for byte", body, IMAGE)
            check("the App's origin may read it",
                  reply.get("access-control-allow-origin"), DEPLOYED)

            code, _, _ = request(port, "GET", "/api/snapshot")
            check("no origin, still served", code, 200)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
    finally:
        shutil.rmtree(card, ignore_errors=True)

    print(f"image: {'ok' if FAIL == 0 else str(FAIL) + ' failures'}")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
