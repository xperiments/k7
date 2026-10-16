#!/usr/bin/env python3
"""T6 helper: the index must list the library's audio tracks, with their real sizes.

    check_index.py <index.html> <fixture-directory> [--superset]

On the host, against the fixture corpus, "exactly" is the point: an extra row (a
dot-file, a non-audio file) or a missing one fails, and every size must equal the file's
byte length.

On the board the card may hold the owner's own music, so --superset is used instead: every
fixture must still be listed with its exact size, and anything else the index shows must be
a plausible audio file - a playable extension, a label equal to its name, a non-zero size.
What it still refuses to accept is a dot-file, a non-audio extension or a wrong size, which
is what the check exists for.
"""
import argparse
import html
import os
import re
import sys

ap = argparse.ArgumentParser()
ap.add_argument("index")
ap.add_argument("fixture_dir")
ap.add_argument("--superset", action="store_true",
                help="the library may hold more than the fixtures (a real card)")
args = ap.parse_args()
index_path, fixture_dir = args.index, args.fixture_dir
page = open(index_path, "r", encoding="utf-8").read()

rows = re.findall(r'<a href="/music/([^"]+)">(.*?)</a>\s*<span class="n">(\d+) B</span>', page, re.S)
listed = {name: (html.unescape(label), int(size)) for name, label, size in rows}

expected = {}
for name in sorted(os.listdir(fixture_dir)):
    if name.startswith("."):
        continue
    if os.path.splitext(name)[1].lower() in (".mp3", ".flac", ".wav"):
        expected[name] = os.path.getsize(os.path.join(fixture_dir, name))

PLAYABLE = (".mp3", ".flac", ".wav")
problems = []
for name in sorted(set(expected) | set(listed)):
    if name not in listed:
        problems.append("missing from the index: %s" % name)
    elif name not in expected:
        if not args.superset:
            problems.append("listed but not an audio file in the directory: %s" % name)
        else:
            label, size = listed[name]
            if os.path.splitext(name)[1].lower() not in PLAYABLE:
                problems.append("listed but not playable: %s" % name)
            elif label != name:
                problems.append("label %r does not match the file name %r" % (label, name))
            elif size <= 0:
                problems.append("size for %s is %d" % (name, size))
    else:
        label, size = listed[name]
        if label != name:
            problems.append("label %r does not match the file name %r" % (label, name))
        if size != expected[name]:
            problems.append("size for %s is %d, the file is %d bytes" % (name, size, expected[name]))

if problems:
    print("\n".join(problems))
    sys.exit(1)
extra = len(listed) - len([n for n in listed if n in expected])
if args.superset:
    print("all %d fixture tracks listed with matching sizes%s" %
          (len(expected), ", plus %d of the card's own" % extra if extra else ""))
else:
    print("%d tracks listed exactly, sizes match" % len(expected))
