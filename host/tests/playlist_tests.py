#!/usr/bin/env python3
"""Playlists over HTTP: they are files on the card, and the broadcast walks one.

A Playlist is `k7.playlists/<id>.k7list` at the card's root (ADR 0095), written by the app
when the card is in a computer and by the device when it is in the player (ADR 0094). What is
proven here is the five routes: the index with each list's fingerprint, one list's rows, a PUT
that stores a file, a DELETE that removes one, and the select command — plus every refusal a
bad body, a missing list or a lost selection earns.

The server runs against a temporary card, so nothing here touches the fixtures tree.
Usage: playlist_tests.py SERVER FIXTURES_DIR
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

# The two fixture files the lists name.
TRACKS = ["tone-44100.flac", "tone-32000.mp3"]

LATENIGHT = {
    "id": 0xAAAA000000000001,
    "name": "Late night",
    "created": 1767225600,
    "rows": TRACKS,
}
EMPTY = {"id": 0xCCCC000000000003, "name": "Empty", "created": 1767225600, "rows": []}


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


def list_text(entry):
    """A list's file, as the app writes it (docs/specs/playlist-format.md)."""
    lines = [
        "K7LIST 1",
        f"id {entry['id']:016x}",
        f"name {entry['name']}",
        f"created {entry['created']}",
        f"rows {len(entry['rows'])}",
    ]
    lines += entry["rows"]
    return ("\n".join(lines) + "\n").encode()


def write_list(card, entry):
    directory = os.path.join(card, "k7.playlists")
    os.makedirs(directory, exist_ok=True)
    path = os.path.join(directory, f"{entry['id']:016x}.k7list")
    with open(path, "wb") as f:
        f.write(list_text(entry))


def start(card, port):
    env = dict(os.environ)
    env.pop("XTAPE_SNAPSHOT", None)
    proc = subprocess.Popen(
        [SERVER, "--music-dir", os.path.join(card, "music"), "--port", str(port)],
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


def make_card():
    card = tempfile.mkdtemp(prefix="xtpl")
    music = os.path.join(card, "music")
    os.makedirs(music)
    for name in TRACKS:
        shutil.copy(os.path.join(FIXTURES, name), os.path.join(music, name))
    # One Track the list does not name, so "play this instead" has something to play.
    shutil.copy(os.path.join(FIXTURES, TRACKS[0]), os.path.join(music, "outside.flac"))
    for entry in (LATENIGHT, EMPTY):
        write_list(card, entry)
    return card


def main():
    card = make_card()
    port = free_port()
    server = start(card, port)
    try:
        status, doc = request(port, "GET", "/api/playlists")
        check("playlists answers", status, 200)
        check("two lists", len(doc["lists"]), 2)
        check("nothing selected yet", doc["selected"], None)
        check("and nothing walking", doc["walking"], False)
        names = [l["name"] for l in doc["lists"]]
        check("names, in id order", names, ["Late night", "Empty"])
        check("row count", doc["lists"][0]["rows"], 2)
        check("created", doc["lists"][0]["created"], 1767225600)
        check("the index carries a fingerprint", len(doc["lists"][0]["fingerprint"]), 16)

        latelight_hex = f"{LATENIGHT['id']:016x}"
        status, doc = request(port, "GET", f"/api/playlists/{latelight_hex}")
        check("one list answers", status, 200)
        check("its rows are paths", doc["paths"], LATENIGHT["rows"])
        check("its name", doc["name"], "Late night")
        check("its fingerprint", len(doc["fingerprint"]), 16)

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
        for name in sorted(os.listdir(os.path.join(card, "music"))):
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

        # A PUT stores the file it is given, and answers once it is on the card.
        new = {"id": 0xDDDD000000000004, "name": "Written here", "created": 1767225700,
               "rows": [TRACKS[0]]}
        new_hex = f"{new['id']:016x}"
        status, doc = request(port, "PUT", f"/api/playlists/{new_hex}", list_text(new),
                              "text/plain; charset=utf-8")
        check("a PUT is accepted", status, 200)
        check("and reports its rows", doc["rows"], 1)
        check("and its fingerprint", len(doc["fingerprint"]), 16)
        path = os.path.join(card, "k7.playlists", f"{new_hex}.k7list")
        check_true("the file is on the card", os.path.exists(path))
        status, doc = request(port, "GET", f"/api/playlists/{new_hex}")
        check("it reads back", doc["paths"], [TRACKS[0]])
        status, doc = request(port, "GET", "/api/playlists")
        check("and the index has it", len(doc["lists"]), 3)

        # A body that is not a list leaves the card as it was.
        status, _ = request(port, "PUT", f"/api/playlists/{new_hex}",
                            b"K7LIST 9\nid 0000000000000004\n", "text/plain")
        check("a bad body is 400", status, 400)
        status, doc = request(port, "GET", f"/api/playlists/{new_hex}")
        check("and the old file is untouched", doc["paths"], [TRACKS[0]])

        # The id inside the body must agree with the path.
        wrong = dict(new, id=0xEEEE000000000005)
        status, _ = request(port, "PUT", f"/api/playlists/{new_hex}", list_text(wrong),
                            "text/plain")
        check("an id that disagrees is 400", status, 400)

        # A DELETE removes one, and an unknown one is 404.
        status, doc = request(port, "DELETE", f"/api/playlists/{new_hex}")
        check("a DELETE is accepted", status, 200)
        check("and says what it removed", doc["removed"], True)
        check_true("the file is gone", not os.path.exists(path))
        status, _ = request(port, "DELETE", f"/api/playlists/{new_hex}")
        check("deleting it twice is 404", status, 404)

        # Deleting the list being walked ends the walk and clears the selection.
        status, _ = request(port, "POST", "/api/playlists/select",
                            json.dumps({"id": latelight_hex}).encode())
        check("selected before the delete", status, 200)
        status, _ = request(port, "DELETE", f"/api/playlists/{latelight_hex}")
        check("deleting the walked list is accepted", status, 200)
        status, doc = request(port, "GET", "/api/playlists")
        check("the selection is cleared", doc["selected"], None)
        check("and nothing walks", doc["walking"], False)

        # More lists than the format allows is refused as a whole.
        for n in range(70):
            write_list(card, {"id": 0x1000000000000000 + n, "name": f"Filler {n}",
                              "created": 1767225700, "rows": []})
        status, doc = request(port, "GET", "/api/playlists")
        check("a directory over its cap is refused", status, 503)
    finally:
        server.kill()
        server.wait()
        shutil.rmtree(card, ignore_errors=True)

    print(f"playlists: {'ok' if FAIL == 0 else 'FAILED'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
