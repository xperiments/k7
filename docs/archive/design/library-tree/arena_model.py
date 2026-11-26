#!/usr/bin/env python3
"""Order-of-magnitude model for a boot-built library arena and its JSON.

Not a measurement of the device: it computes sizes from a synthetic tree so the
document can quote a budget instead of a guess. Names are representative of a
real card (artist / album / NN - title.ext), not worst case.
"""
import json
from dataclasses import dataclass, field


@dataclass
class Node:
    name: str
    path: str
    is_dir: bool
    size: int = 0
    fmt: str = ""
    children: list = field(default_factory=list)


def build(artists, albums, tracks, name_len=28):
    root = Node("music", "", True)
    for a in range(artists):
        an = f"Artist {a:02d} With A Longer Name"
        ad = Node(an, an, True)
        root.children.append(ad)
        for b in range(albums):
            bn = f"{2010 + b} - Album Title Number {b:02d}"
            bd = Node(bn, f"{an}/{bn}", True)
            ad.children.append(bd)
            for t in range(tracks):
                title = f"{t + 1:02d} - A Track Title Of About {name_len} Chars"
                ext = (".mp3", ".flac", ".wav")[t % 3]
                tn = title + ext
                bd.children.append(Node(tn, f"{bd.path}/{tn}", False,
                                        size=4_000_000 + (t * 12345) % 30_000_000,
                                        fmt=ext[1:]))
    return root


def stats(root):
    dirs = tracks = 0
    name_bytes = path_bytes = 0
    total_size = 0

    def walk(n):
        nonlocal dirs, tracks, name_bytes, path_bytes, total_size
        if n.is_dir:
            dirs += 1
        else:
            tracks += 1
            total_size += n.size
        name_bytes += len(n.name.encode())
        path_bytes += len(n.path.encode())
        for c in n.children:
            walk(c)

    walk(root)
    return dirs, tracks, name_bytes, path_bytes, total_size


def to_json(n):
    if n.is_dir:
        d = {"name": n.name, "path": n.path, "dirs": [], "tracks": []}
        for c in n.children:
            if c.is_dir:
                d["dirs"].append(to_json(c))
            else:
                d["tracks"].append({"name": c.name, "path": c.path,
                                    "size": c.size, "format": c.fmt})
        return d
    return {"name": n.name, "path": n.path, "size": n.size, "format": n.fmt}


# 32-byte node: parent/name_off/name_len/flags/size/first_child/next_sibling/format
NODE = 32
TRACK_INDEX = 4  # uint32 node id per track
EDGE = 4         # uint32 child id per node except the root


def report(label, root):
    dirs, tracks, name_bytes, path_bytes, total = stats(root)
    nodes = dirs + tracks
    binary = 64 + nodes * NODE + (nodes - 1) * EDGE + tracks * TRACK_INDEX + name_bytes
    doc = {"root": "/sdcard/music", "count": tracks, "bytes": total,
           "tree": to_json(root)}
    js = json.dumps(doc, separators=(",", ":"), ensure_ascii=False).encode()
    print(f"{label}: {dirs} dirs, {tracks} tracks, {total/1e9:.1f} GB")
    print(f"  binary : {binary/1e6:.2f} MB  ({(binary/max(tracks,1)):.0f} B/track)")
    print(f"  json   : {len(js)/1e6:.2f} MB  ({(len(js)/max(tracks,1)):.0f} B/track)")
    print(f"  names  : {name_bytes/1e3:.0f} KB ({name_bytes/max(tracks+dirs,1):.0f} B/node)")
    print(f"  paths  : {path_bytes/1e3:.0f} KB if stored per node")


report("100 tracks  (10 artists x 1 album x 10)", build(10, 1, 10))
report("1 000 tracks (20 x 5 x 10)", build(20, 5, 10))
report("10 000 tracks (40 x 10 x 25)", build(40, 10, 25))
