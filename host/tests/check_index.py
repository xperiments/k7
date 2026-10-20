#!/usr/bin/env python3
"""T6 helper: the index must list the Library's Tracks, in playlist order.

    check_index.py <index.html> <fixture-directory> [--superset | --card]

On the host, against the fixture corpus, "exactly" is the point: an extra row
(a dot-file, a non-audio file) or a missing one fails, the order must be the
depth-first name-sorted playlist, and every size must equal the file's byte
length.

On the board the card may hold the owner's own music, so --superset is used
instead: every fixture must still be listed with its exact size, and anything
else the index shows must be a plausible audio path. --card checks that every
row is self-consistent without a fixture directory.
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

AUDIO = (".mp3", ".flac", ".wav")


def sort_key(name):
    """The Library's playlist order: ASCII case-folded, then exact."""
    folded = "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in name)
    return (folded, name)


def expected_tracks(root):
    out = []

    def walk(directory, prefix):
        try:
            entries = sorted(os.listdir(directory), key=sort_key)
        except OSError:
            return
        for name in entries:
            if (name.startswith(".") or ".." in name or "/" in name or "\\" in name
                    or len(name) > 128):
                continue
            full = os.path.join(directory, name)
            if os.path.islink(full):
                continue
            rel = prefix + name
            if os.path.isdir(full):
                walk(full, rel + "/")
            elif os.path.splitext(name)[1].lower() in AUDIO:
                out.append((rel, os.path.getsize(full)))

    walk(root, "")
    return out


page = open(index_path, "r", encoding="utf-8").read()
rows = re.findall(r'<a href="/music/([^"]+)">(.*?)</a>\s*<span class="n">(\d+) B</span>', page, re.S)
# href is percent-encoded; the label is HTML-escaped. Both name the same path.
listed = [
    (unquote(href), html.unescape(label), int(size))
    for href, label, size in rows
]


def self_consistent(entry):
    path, label, size = entry
    for segment in path.split("/"):
        if segment.startswith(".") or ".." in segment or "\\" in segment:
            return "listed but traversal-shaped: %s" % path
    if os.path.splitext(path)[1].lower() not in AUDIO:
        return "listed but not playable: %s" % path
    if label != path:
        return "label %r does not match the path %r" % (label, path)
    if size <= 0:
        return "size for %s is %d" % (path, size)
    return None


if args.card:
    problems = [p for p in (self_consistent(e) for e in listed) if p]
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    print("%d tracks listed on the card, every label, path and size self-consistent" % len(listed))
    sys.exit(0)

expected = expected_tracks(fixture_dir)
by_path = {path: (label, size) for path, label, size in listed}

if args.superset:
    problems = []
    for path, size in expected:
        if path not in by_path:
            problems.append("missing from the index: %s" % path)
            continue
        label, got = by_path[path]
        if label != path:
            problems.append("label %r does not match the path %r" % (label, path))
        if got != size:
            problems.append("size for %s is %d, the file is %d bytes" % (path, got, size))
    for entry in listed:
        if entry[0] in dict(expected):
            continue
        problem = self_consistent(entry)
        if problem:
            problems.append(problem)
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    extra = len(listed) - len([e for e in listed if e[0] in dict(expected)])
    print("all %d fixture tracks listed with matching sizes%s" %
          (len(expected), ", plus %d of the card's own" % extra if extra else ""))
    sys.exit(0)

problems = []
got_order = [path for path, _label, _size in listed]
want_order = [path for path, _size in expected]
if got_order != want_order:
    first = 0
    while (first < len(got_order) and first < len(want_order)
           and got_order[first] == want_order[first]):
        first += 1
    got = got_order[first] if first < len(got_order) else "(end)"
    want = want_order[first] if first < len(want_order) else "(end)"
    problems.append("playlist order differs at row %d: got %r, want %r" %
                    (first, got, want))
for path, size in expected:
    if path not in by_path:
        problems.append("missing from the index: %s" % path)
        continue
    label, got = by_path[path]
    if label != path:
        problems.append("label %r does not match the path %r" % (label, path))
    if got != size:
        problems.append("size for %s is %d, the file is %d bytes" % (path, got, size))
for entry in listed:
    if entry[0] not in dict(expected):
        problems.append("listed but not a Track in the tree: %s" % entry[0])
if problems:
    print("\n".join(problems))
    sys.exit(1)
print("%d tracks listed exactly, in playlist order, sizes match" % len(expected))
