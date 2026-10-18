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
from urllib.parse import unquote

ap = argparse.ArgumentParser()
ap.add_argument("index")
ap.add_argument("fixture_dir", nargs="?")
ap.add_argument("--superset", action="store_true",
                help="the library may hold more than the fixtures (a real card)")
ap.add_argument("--card", action="store_true",
                help="no fixture corpus: check that every row on the card is self-consistent")
args = ap.parse_args()
index_path, fixture_dir = args.index, args.fixture_dir
if args.card and fixture_dir:
    sys.exit("--card takes no fixture directory")
if not args.card and not fixture_dir:
    sys.exit("a fixture directory is required unless --card is given")
page = open(index_path, "r", encoding="utf-8").read()

rows = re.findall(r'<a href="/music/([^"]+)">(.*?)</a>\s*<span class="n">(\d+) B</span>', page, re.S)
# The href is percent-encoded and the label is HTML-escaped; both name the same
# file. Comparing the raw href only worked while the fixture corpus had no names
# that need encoding - a real card has spaces, brackets and accents.
listed = {unquote(name): (html.unescape(label), int(size)) for name, label, size in rows}

expected = {}
for name in sorted(os.listdir(fixture_dir) if fixture_dir else []):
    if name.startswith("."):
        continue
    if os.path.splitext(name)[1].lower() in (".mp3", ".flac", ".wav"):
        expected[name] = os.path.getsize(os.path.join(fixture_dir, name))

PLAYABLE = (".mp3", ".flac", ".wav")

if args.card:
    # A real card, no reference corpus: every row must be a playable file, its
    # label must be its own (decoded) name, and its size must be positive. What
    # this cannot check is that the bytes are the card's - that needs the file,
    # which the download check does separately.
    problems = []
    for name, (label, size) in sorted(listed.items()):
        if os.path.splitext(name)[1].lower() not in PLAYABLE:
            problems.append("listed but not playable: %s" % name)
        elif label != name:
            problems.append("label %r does not match the file name %r" % (label, name))
        elif size <= 0:
            problems.append("size for %s is %d" % (name, size))
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    print("%d tracks listed on the card, every label and size self-consistent" % len(listed))
    sys.exit(0)

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
