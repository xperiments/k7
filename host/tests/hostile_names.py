#!/usr/bin/env python3
"""Real music libraries are not called `tone-44100.flac`.

This is the part of "test with real files" that can be checked without a card: names
with spaces, accents, apostrophes, percent signs, semicolons and dashes, plus the
macOS `._` sidecar files that appear on a card written from a Mac. It runs the server
over such a library and checks the two places a name travels:

  1. the index - listed, HTML-escaped as text, and never turned into a link (the
     device serves no Track, so there is no `/music/` href to break),
  2. the live stream's ICY metadata - the title is a well-formed metadata block, which
     is what an apostrophe or semicolon in a name can break.

    python3 host/tests/hostile_names.py ./host/build/bin/icecast_host /tmp/hostile

Exit status is the number of failures.
"""
import html
import os
import re
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

PORT = 18055
BASE = f"http://127.0.0.1:{PORT}"

NAMES = [
    "My Song.flac",
    "Café música.flac",
    "it's a test.flac",
    "100% pure.flac",
    "semi;colon - dash.flac",
]
# macOS writes these next to every file it copies to a FAT volume; they are not audio.
IGNORED = ["._appledouble.flac"]

fails = 0


def check(cond, what):
    global fails
    print(("  ok    " if cond else "  FAIL  ") + what)
    if not cond:
        fails += 1


def build_library(root, source):
    music = os.path.join(root, "music")
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(music)
    for name in NAMES + IGNORED:
        shutil.copy(source, os.path.join(music, name))
    return music


def stream_titles(seconds=8.0):
    """Every StreamTitle the live stream announces in a window."""
    s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
    s.sendall(b"GET /stream HTTP/1.0\r\nHost: t\r\nIcy-MetaData: 1\r\n\r\n")
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    head, body = buf.split(b"\r\n\r\n", 1)
    metaint = int(re.search(rb"icy-metaint:\s*(\d+)", head).group(1))
    s.settimeout(1.0)
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = s.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            break
        body += chunk
    s.close()

    titles, i, since = [], 0, 0
    while i < len(body):
        take = min(metaint - since, len(body) - i)
        i += take
        since += take
        if since == metaint:
            since = 0
            if i >= len(body):
                break
            blocks = body[i]
            i += 1
            if blocks:
                blk = body[i:i + 16 * blocks]
                i += 16 * blocks
                m = re.match(rb"StreamTitle='(.*?)';", blk)
                if m:
                    titles.append(m.group(1).decode("utf-8", "replace"))
    return titles


def main():
    server, source = sys.argv[1], sys.argv[2]
    music = build_library("/tmp/hostile_run", source)
    print(f"library: {len(NAMES)} awkward names (+{len(IGNORED)} that must be ignored)")

    proc = subprocess.Popen([server, "--music-dir", music, "--port", str(PORT)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try:
                urllib.request.urlopen(BASE + "/", timeout=1).read()
                break
            except Exception:
                time.sleep(0.25)
        else:
            print("FATAL: the server did not come up")
            return 1

        page = urllib.request.urlopen(BASE + "/", timeout=5).read().decode("utf-8")
        # The row text is HTML-escaped, so the page has to be unescaped before a name
        # can be looked for in it.
        text = html.unescape(page)

        for name in NAMES:
            check(name in text, f"the index lists {name!r}")
        for name in IGNORED:
            check(name not in text, f"the index ignores {name!r} (macOS sidecar)")
        check("/music/" not in page,
              "the index links no Track: there is no /music/ href (the device serves no Track)")
        for name in NAMES:
            status = 404
            try:
                urllib.request.urlopen(BASE + "/music/" + urllib.parse.quote(name), timeout=5)
                status = 200
            except urllib.error.HTTPError as e:
                status = e.code
            check(status == 404, f"requesting {name!r} by path is a 404")

        # The announced title is the file's name without its extension, with apostrophes
        # replaced by spaces: an ICY metadata block is delimited by apostrophes and
        # semicolons, so a name containing them cannot be sent verbatim. That substitution
        # is the documented behaviour, and this is the place it gets checked.
        expected = [n.rsplit(".", 1)[0].replace("'", " ") for n in NAMES]
        titles = stream_titles()
        check(len(titles) > 0, f"the stream announces titles ({titles[:3]}...)")
        for t in titles:
            check(t in expected, f"the announced title is a well-formed name: {t!r}")
    finally:
        proc.terminate()
        proc.wait(timeout=10)

    print(f"\n{len(NAMES)} awkward names: {'all checks passed' if fails == 0 else str(fails) + ' FAILED'}")
    return fails


if __name__ == "__main__":
    sys.exit(main())
