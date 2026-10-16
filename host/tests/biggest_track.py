#!/usr/bin/env python3
"""Print the largest track in a server's index, as "name size", or nothing.

    biggest_track.py http://127.0.0.1:18000 [minimum-bytes]

T9 needs one file big enough that a deliberately slow reader keeps the download
in flight; on the board the only files available are the ones on the card, so the
index is the place to find out. Prints nothing when every track is smaller than
the minimum (the shell test then skips the check rather than pretending).
"""
import re
import sys
import urllib.request

base = sys.argv[1].rstrip("/")
minimum = int(sys.argv[2]) if len(sys.argv) > 2 else 2_000_000

page = urllib.request.urlopen(base + "/", timeout=15).read().decode("utf-8", "replace")
rows = re.findall(r'href="/music/([^"]+)">.*?</a>\s*<span class="n">(\d+) B</span>', page, re.S)
rows = [(name, int(size)) for name, size in rows]
if not rows:
    sys.exit(0)
name, size = max(rows, key=lambda r: r[1])
if size < minimum:
    sys.exit(0)
print("%s %d" % (name, size))
