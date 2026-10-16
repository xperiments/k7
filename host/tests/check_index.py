#!/usr/bin/env python3
"""T6 helper: the index must list exactly the audio tracks, with their real sizes.

    check_index.py <index.html> <fixture-directory>

"Exactly" is the point: an extra row (a dot-file, a non-audio file) or a missing
one fails, and every size must equal the file's byte length.
"""
import html
import os
import re
import sys

index_path, fixture_dir = sys.argv[1], sys.argv[2]
page = open(index_path, "r", encoding="utf-8").read()

rows = re.findall(r'<a href="/music/([^"]+)">(.*?)</a>\s*<span class="n">(\d+) B</span>', page, re.S)
listed = {name: (html.unescape(label), int(size)) for name, label, size in rows}

expected = {}
for name in sorted(os.listdir(fixture_dir)):
    if name.startswith("."):
        continue
    if os.path.splitext(name)[1].lower() in (".mp3", ".flac", ".wav"):
        expected[name] = os.path.getsize(os.path.join(fixture_dir, name))

problems = []
for name in sorted(set(expected) | set(listed)):
    if name not in listed:
        problems.append("missing from the index: %s" % name)
    elif name not in expected:
        problems.append("listed but not an audio file in the directory: %s" % name)
    else:
        label, size = listed[name]
        if label != name:
            problems.append("label %r does not match the file name %r" % (label, name))
        if size != expected[name]:
            problems.append("size for %s is %d, the file is %d bytes" % (name, size, expected[name]))

if problems:
    print("\n".join(problems))
    sys.exit(1)
print("%d tracks listed exactly, sizes match" % len(expected))
