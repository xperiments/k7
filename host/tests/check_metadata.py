#!/usr/bin/env python3
"""T5 helper: ICY metadata must land at exactly metaint *encoded* bytes.

    check_metadata.py <captured-body> [metaint]

The client asked for metadata, so the body is metaint bytes of MP3, then a length
byte (in 16-byte blocks), then that many bytes of "StreamTitle='...';" padded with
zeros - then audio again. Checking the offset rather than searching for the string
is what shows the interval counts encoded bytes.
"""
import sys

body_path = sys.argv[1]
metaint = int(sys.argv[2]) if len(sys.argv) > 2 else 16000

body = open(body_path, "rb").read()
if len(body) < metaint + 17:
    print("not enough body to check: %d bytes" % len(body))
    sys.exit(1)

blocks = body[metaint]
if blocks == 0:
    print("no metadata block at offset %d" % metaint)
    sys.exit(1)

start = metaint + 1
end = start + blocks * 16
if end > len(body):
    print("metadata block runs past the end of the capture")
    sys.exit(1)
block = body[start:end]

if not block.startswith(b"StreamTitle='"):
    print("metadata at %d does not start with StreamTitle=': %r" % (metaint, block[:32]))
    sys.exit(1)
title = block[len(b"StreamTitle='") : block.index(b"';")]
payload = len(b"StreamTitle='") + len(title) + 2
if payload > blocks * 16:
    print("metadata payload does not fit its declared block count")
    sys.exit(1)
if any(block[payload:]):
    print("metadata padding is not zero-filled")
    sys.exit(1)

# And the byte right before the block must be audio, not padding: the interval is
# counted over encoded bytes, so the block sits exactly on the boundary.
print("metadata block at exactly %d: StreamTitle=%r (%d blocks, %d bytes)"
      % (metaint, title.decode("utf-8", "replace"), blocks, blocks * 16))
