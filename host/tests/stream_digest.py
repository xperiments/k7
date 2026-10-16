#!/usr/bin/env python3
"""T11 helper: take one deterministic window of encoded audio out of the live stream.

    stream_digest.py <stream-url> <out.mp3> [seconds]

Why this exists: the host and the board must be shown to produce the same audio,
and the only way to see the board's bytes is to capture its stream. A capture
starts wherever the client connected, so the digest has to be taken over a window
delimited by something intrinsic to the stream. Track changes are exactly that:
the ICY metadata says which track is playing, and a change of title marks a track
boundary.

The script de-interleaves the ICY metadata, finds the first title change, and
writes the audio from there up to the next title change. It prints the span it
used, so two captures can be checked for comparability before their digests are
believed: compare the digest only when the reported span and byte count match.

Note the caveat the specification records: everything here is fixed-point
(decoder and encoder), so host and board must agree byte for byte - unless the
source sample rate differs from the station rate, in which case the resampler's
floating-point kernel is in the path and only a signal-to-noise bound may be
claimed.
"""
import re
import socket
import sys
import time
import urllib.parse

url = sys.argv[1]
out_path = sys.argv[2]
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 12.0

parsed = urllib.parse.urlsplit(url)
host = parsed.hostname or "127.0.0.1"
port = parsed.port or 8000
path = parsed.path or "/stream"

s = socket.create_connection((host, port), timeout=10)
s.sendall(("GET %s HTTP/1.0\r\nHost: %s\r\nIcy-MetaData: 1\r\n\r\n" % (path, host)).encode())

buf = b""
while b"\r\n\r\n" not in buf:
    chunk = s.recv(4096)
    if not chunk:
        sys.exit("no response headers from %s" % url)
    buf += chunk
head, rest = buf.split(b"\r\n\r\n", 1)

metaint = None
for line in head.split(b"\r\n")[1:]:
    if line.lower().startswith(b"icy-metaint:"):
        metaint = int(line.split(b":", 1)[1].strip())
if not metaint:
    sys.exit("the stream did not advertise icy-metaint; cannot find track boundaries")

body = rest
s.settimeout(2.0)
t0 = time.time()
while time.time() - t0 < seconds:
    try:
        chunk = s.recv(65536)
    except socket.timeout:
        continue
    if not chunk:
        break
    body += chunk
s.close()

# ---------------------------------------------------------------- de-interleave
audio = bytearray()
events = []  # (audio length at the transition, title)
last_title = None
i = 0
since = 0
while i < len(body):
    take = min(metaint - since, len(body) - i)
    audio += body[i : i + take]
    i += take
    since += take
    if since == metaint:
        since = 0
        if i >= len(body):
            break
        blocks = body[i]
        i += 1
        if blocks:
            block = body[i : i + 16 * blocks]
            i += 16 * blocks
            m = re.search(rb"StreamTitle='(.*?)';", block)
            title = m.group(1).decode("utf-8", "replace") if m else ""
            if title != last_title:
                events.append((len(audio), title))
                last_title = title

if len(events) < 2:
    sys.exit("only %d title change(s) seen in %.1fs; capture longer or add tracks"
             % (len(events), seconds))

start, first_title = events[0]
end, next_title = events[1]
window = bytes(audio[start:end])
if len(window) < 4096:
    sys.exit("the window between track changes is only %d bytes: not usable" % len(window))

open(out_path, "wb").write(window)
print("span: %r -> %r, %d bytes of encoded audio (from %d title changes)"
      % (first_title, next_title, len(window), len(events)))
