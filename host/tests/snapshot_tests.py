#!/usr/bin/env python3
"""The Snapshot, end to end, against the host server.

Builds an image with an independent writer (this file, not the C module and not the
TypeScript one), points the server at it through XTAPE_SNAPSHOT, and asks the MCP
endpoint the questions the feature exists to answer: a BPM range, a key, a mood, a
range on a field one track does not have, a sort, and the coverage count.

Usage: snapshot_tests.py SERVER FIXTURES_DIR
"""
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

from snapshot_image import build_image, fnv1a64

SERVER = sys.argv[1]
FIXTURES = sys.argv[2]
FAIL = 0

MAGIC = b"XTAPESNP"
UNKNOWN = 0xFF
HAPPY, SAD, AGGRESSIVE, RELAXED, PARTY = 1, 2, 4, 8, 16


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


def rpc(port, body, headers=None):
    payload = json.dumps(body).encode()
    head = (
        f"POST /mcp HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n"
        f"Content-Type: application/json\r\nContent-Length: {len(payload)}\r\n"
    )
    for k, v in (headers or {}).items():
        head += f"{k}: {v}\r\n"
    head += "\r\n"
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    s.sendall(head.encode() + payload)
    data = b""
    while True:
        part = s.recv(65536)
        if not part:
            break
        data += part
    s.close()
    _, _, body_bytes = data.partition(b"\r\n\r\n")
    return json.loads(body_bytes)


def call_tool(port, name, arguments):
    resp = rpc(port, {"jsonrpc": "2.0", "id": 7, "method": "tools/call",
                      "params": {"name": name, "arguments": arguments}})
    text = resp["result"]["content"][0]["text"]
    parsed = json.loads(text)
    return parsed, resp["result"].get("isError", False)


def wait_ready(port):
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.sendall(f"GET / HTTP/1.0\r\nHost: 127.0.0.1:{port}\r\n\r\n".encode())
            s.recv(128)
            s.close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def main():
    tmp = tempfile.mkdtemp(prefix="xtape-snapshot-")
    music = os.path.join(tmp, "music")
    os.makedirs(os.path.join(music, "Alpha", "One"))
    os.makedirs(os.path.join(music, "Beta", "Two"))
    flac = os.path.join(music, "Alpha", "One", "tone.flac")
    mp3 = os.path.join(music, "Beta", "Two", "tone.mp3")
    wav = os.path.join(music, "Beta", "Two", "loose.wav")
    shutil.copy(os.path.join(FIXTURES, "tone-44100.flac"), flac)
    shutil.copy(os.path.join(FIXTURES, "tone-32000.mp3"), mp3)
    shutil.copy(os.path.join(FIXTURES, "tone-48000.wav"), wav)  # deliberately not in the image

    image = build_image([
        dict(path="Alpha/One/tone.flac", size=os.path.getsize(flac), bpm_x10=1280, key=8,
             energy=80, dance=60, valence=50, arousal=80, mood=HAPPY | PARTY, conf=70,
             src=0x10, title="Alpha Tone", artist="The Alphas", album="One",
             genre="electronic"),
        dict(path="Beta/Two/tone.mp3", size=os.path.getsize(mp3), bpm_x10=1000, key=5,
             energy=30, dance=UNKNOWN, valence=UNKNOWN, arousal=UNKNOWN, mood=RELAXED,
             conf=UNKNOWN, src=0x03, title="Beta Tone", artist="The Betas", album="Two",
             genre="jazz"),
    ])
    image_path = os.path.join(tmp, "xtape.db")
    with open(image_path, "wb") as f:
        f.write(image)

    port = free_port()
    env = dict(os.environ, XTAPE_SNAPSHOT=image_path)
    proc = subprocess.Popen(
        [SERVER, "--music-dir", music, "--port", str(port)],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    try:
        if not wait_ready(port):
            print("FAIL the server did not come up")
            print(proc.stdout.read().decode(errors="replace")[-3000:])
            return 1

        rpc(port, {"jsonrpc": "2.0", "id": 1, "method": "initialize",
                   "params": {"protocolVersion": "2025-11-25", "capabilities": {},
                              "clientInfo": {"name": "snapshot_tests", "version": "1"}}})

        # --- stats: coverage counts the current Library, not the image
        stats, err = call_tool(port, "library_stats", {})
        check("stats is not an error", err, False)
        check("three tracks", stats["tracks"], 3)
        check("two analysed", stats["snapshot"]["coverage"]["analysed"], 2)
        check("coverage percent", stats["snapshot"]["coverage"]["percent"], 66)
        check("snapshot state", stats["snapshot"]["state"], "ok")
        check("rows in the image", stats["snapshot"]["rows"], 2)

        # --- a BPM range
        found, err = call_tool(port, "library_search", {"bpm_min": 120, "bpm_max": 130})
        check("bpm range is not an error", err, False)
        check("bpm range matched", found["matched"], 1)
        track = found["tracks"][0]
        check("the flac", track["path"], "Alpha/One/tone.flac")
        check("bpm is presented with its decimal", track["analysis"]["bpm"], 128.0)
        check("key is Camelot", track["analysis"]["key"], "8A")
        check("title comes from the image", track["analysis"]["title"], "Alpha Tone")
        check("mood names", track["analysis"]["mood"], ["happy", "party"])

        # --- a key, spelled as a note name
        found, err = call_tool(port, "library_search", {"key": ["A minor"]})
        check("key filter is not an error", err, False)
        check("A minor matched the 8A track", found["matched"], 1)

        # --- a mood
        found, err = call_tool(port, "library_search", {"mood": ["relaxed"]})
        check("mood filter", found["matched"], 1)
        check("the mp3", found["tracks"][0]["path"], "Beta/Two/tone.mp3")
        check("an unknown danceability is null", found["tracks"][0]["analysis"]["danceability"], None)
        check("tagged flags travel", found["tracks"][0]["analysis"]["tagged"], ["bpm", "key"])

        # --- a range on a field a track does not have excludes it
        found, err = call_tool(port, "library_search", {"energy_min": 70})
        check("energy 70+", found["matched"], 1)
        check("and it is the flac", found["tracks"][0]["path"], "Alpha/One/tone.flac")

        # --- name search reaches a tagged title the path does not contain
        found, err = call_tool(port, "library_search", {"query": "alph"})
        check("query finds the tagged artist", found["matched"], 1)

        # --- an unanalysed track still matches by name, and carries no analysis
        found, err = call_tool(port, "library_search", {"query": "loose"})
        check("unanalysed track found by name", found["matched"], 1)
        check("and has no analysis", found["tracks"][0]["analysis"], None)

        # --- sort and page
        found, err = call_tool(port, "library_search", {"sort": "bpm", "query": "tone"})
        check("both tones", found["matched"], 2)
        check("slowest first", [t["path"] for t in found["tracks"]],
              ["Beta/Two/tone.mp3", "Alpha/One/tone.flac"])
        found, err = call_tool(port, "library_search",
                               {"sort": "bpm", "query": "tone", "offset": 1, "limit": 1})
        check("the second page", [t["path"] for t in found["tracks"]], ["Alpha/One/tone.flac"])
        check("the last page is not truncated", found["truncated"], False)
        found, err = call_tool(port, "library_search", {"sort": "bpm", "query": "tone", "limit": 1})
        check("a cut page says so", found["truncated"], True)

        # --- an analysis filter on a device with no snapshot is an error, not silence
        # (covered by the host unit tests; here the device has one)
        found, err = call_tool(port, "library_search", {"genre": "electro"})
        check("genre substring", found["matched"], 1)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"snapshot integration: {'FAILED' if FAIL else 'ok'} ({FAIL} failures)")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
