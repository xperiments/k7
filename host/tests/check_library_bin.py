#!/usr/bin/env python3
"""Independent reader for GET /api/library.bin.

It knows only the wire layout: a 64-byte header, 32-byte nodes, a u32 child-edge
array, a u32 track index and a string table. It does not link the firmware's
library code, so a wrong offset, length or CRC will disagree with this file.
"""
import argparse
import binascii
import os
import struct
import sys

MAGIC = 0x424C5458
NODE_NONE = 0xFFFFFFFF
HEADER = struct.Struct("<IHH8IQ2IQ")
NODE = struct.Struct("<QIIIHHBBHI")
AUDIO = {".mp3", ".flac", ".wav"}

assert HEADER.size == 64
assert NODE.size == 32


def fixture_tracks(root):
    tracks = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(
            d for d in dirnames
            if not d.startswith(".") and not os.path.islink(os.path.join(dirpath, d))
        )
        for name in sorted(filenames):
            if name.startswith(".") or os.path.islink(os.path.join(dirpath, name)):
                continue
            if os.path.splitext(name)[1].lower() not in AUDIO:
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            tracks[rel] = os.path.getsize(full)
    return tracks


def parse(data):
    if len(data) < HEADER.size:
        raise ValueError("shorter than the header")
    (magic, version, header_size, flags, node_count, edge_count, track_count,
     string_bytes, crc, generation, root_name_off, library_bytes,
     _r0, _r1, _r2) = HEADER.unpack_from(data, 0)
    if magic != MAGIC:
        raise ValueError(f"magic is 0x{magic:08x}, want 0x{MAGIC:08x}")
    if version != 1:
        raise ValueError(f"version is {version}")
    if header_size != 64:
        raise ValueError(f"header_size is {header_size}")
    expect = 64 + node_count * NODE.size + edge_count * 4 + track_count * 4 + string_bytes
    if len(data) != expect:
        raise ValueError(f"length is {len(data)}, expected {expect}")
    payload = data[64:]
    if (binascii.crc32(payload) & 0xFFFFFFFF) != crc:
        raise ValueError(f"CRC is 0x{crc:08x}, payload is 0x{binascii.crc32(payload) & 0xFFFFFFFF:08x}")
    if node_count == 0:
        raise ValueError("no root node")
    if not (flags & 0x1):
        raise ValueError("the little-endian flag is not set")

    nodes_off = 64
    edges_off = nodes_off + node_count * NODE.size
    tracks_off = edges_off + edge_count * 4
    strings_off = tracks_off + track_count * 4
    strings = data[strings_off:strings_off + string_bytes]

    nodes = []
    for i in range(node_count):
        (size, name_off, parent, child_edge, child_count, name_len,
         kind, fmt, _node_flags, track_index) = NODE.unpack_from(data, nodes_off + i * NODE.size)
        if name_off + name_len > len(strings):
            raise ValueError(f"node {i} name is outside the string table")
        if kind not in (0, 1):
            raise ValueError(f"node {i} has kind {kind}")
        nodes.append({
            "size": size, "name_off": name_off, "parent": parent,
            "child_edge": child_edge, "child_count": child_count,
            "name_len": name_len, "kind": kind, "format": fmt,
            "track_index": track_index,
        })
    edges = [struct.unpack_from("<I", data, edges_off + i * 4)[0] for i in range(edge_count)]
    tracks = [struct.unpack_from("<I", data, tracks_off + i * 4)[0] for i in range(track_count)]

    def name(i):
        n = nodes[i]
        return strings[n["name_off"]:n["name_off"] + n["name_len"]].decode("utf-8")

    def path(i):
        parts = []
        seen = 0
        while i != 0:
            if i == NODE_NONE or i >= node_count:
                raise ValueError("broken parent chain")
            parts.append(name(i))
            i = nodes[i]["parent"]
            seen += 1
            if seen > node_count:
                raise ValueError("parent cycle")
        return "/".join(reversed(parts))

    if nodes[0]["parent"] != NODE_NONE:
        raise ValueError("root has a parent")
    if nodes[0]["kind"] != 0:
        raise ValueError("root is not a directory")
    if root_name_off != nodes[0]["name_off"]:
        raise ValueError("root_name_off does not point at the root node's name")
    if edge_count != node_count - 1:
        raise ValueError(f"edge_count is {edge_count}, expected {node_count - 1}")

    referenced = [0] * node_count
    for i, n in enumerate(nodes):
        if n["kind"] != 0:
            if n["child_count"] or n["child_edge"] != NODE_NONE:
                raise ValueError(f"track node {i} has children")
            continue
        if n["child_count"] == 0:
            if n["child_edge"] != NODE_NONE:
                raise ValueError(f"empty dir {i} has an edge offset")
            continue
        if n["child_edge"] == NODE_NONE or n["child_edge"] + n["child_count"] > edge_count:
            raise ValueError(f"dir {i} edge range is outside the edge array")
        for c in edges[n["child_edge"]:n["child_edge"] + n["child_count"]]:
            if c == 0 or c >= node_count:
                raise ValueError(f"dir {i} has an invalid child")
            if nodes[c]["parent"] != i:
                raise ValueError(f"node {c} parent is {nodes[c]['parent']}, want {i}")
            referenced[c] += 1
    for i in range(1, node_count):
        if referenced[i] != 1:
            raise ValueError(f"node {i} is referenced {referenced[i]} times")

    got = {}
    total = 0
    for i, n in enumerate(nodes):
        if n["kind"] != 1:
            continue
        if n["track_index"] >= track_count or tracks[n["track_index"]] != i:
            raise ValueError(f"track node {i} is not in the track index")
        p = path(i)
        got[p] = n["size"]
        total += n["size"]
    if len(got) != track_count:
        raise ValueError(f"{len(got)} distinct paths for {track_count} tracks")
    if total != library_bytes:
        raise ValueError(f"library_bytes is {library_bytes}, tracks sum to {total}")
    if len(set(tracks)) != track_count:
        raise ValueError("track index has duplicates")
    for i, t in enumerate(tracks):
        if t >= node_count or nodes[t]["kind"] != 1 or nodes[t]["track_index"] != i:
            raise ValueError(f"track index entry {i} is not a track")
    for i, n in enumerate(nodes):
        if n["kind"] == 0 and n["track_index"] != NODE_NONE:
            raise ValueError(f"dir {i} has a track index")
        if n["kind"] == 1 and n["track_index"] >= track_count:
            raise ValueError(f"track {i} has no index")
    return got, node_count, edge_count, track_count, strings, generation, flags


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("blob_file")
    ap.add_argument("root")
    ap.add_argument("--state", default="ok", choices=["ok", "empty", "no_card", "no_dir"])
    ap.add_argument("--truncated", action="store_true")
    ap.add_argument("--header-only", action="store_true")
    ap.add_argument("--subset", action="store_true")
    args = ap.parse_args()

    with open(args.blob_file, "rb") as f:
        data = f.read()
    try:
        got, nodes, edges, tracks, _strings, generation, flags = parse(data)
    except (ValueError, UnicodeDecodeError) as e:
        print(f"blob: {e}")
        return 1
    if bool(flags & 0x2) != args.truncated:
        print(f"truncated flag is {bool(flags & 0x2)}, want {args.truncated}")
        return 1
    if args.header_only:
        print(f"binary header: {nodes} nodes, {edges} edges, {tracks} tracks, "
              f"generation {generation}, CRC and parentage verified")
        return 0

    if args.state in ("empty", "no_card", "no_dir"):
        if nodes != 1 or edges != 0 or tracks != 0 or got:
            print(f"state {args.state} should be one empty root, got {nodes}/{edges}/{tracks}")
            return 1
        print(f"binary header: state {args.state}, empty tree, generation {generation}")
        return 0

    want = fixture_tracks(args.root)
    if not args.truncated:
        missing = sorted(set(want) - set(got))
        extra = sorted(set(got) - set(want))
        if missing:
            print(f"os.walk() tracks absent from the blob: {missing[:3]}")
            return 1
        if extra:
            print(f"blob tracks absent from os.walk(): {extra[:3]}")
            return 1
    elif args.subset and len(got) >= len(want):
        print(f"wanted a strict subset, got {len(got)} of {len(want)} tracks")
        return 1
    for p, size in got.items():
        if p not in want:
            print(f"blob path not found by os.walk(): {p}")
            return 1
        if want[p] != size:
            print(f"{p}: blob says {size}, file is {want[p]}")
            return 1
    print(f"binary matches os.walk(): {len(got)}/{len(want)} tracks, "
          f"{sum(got.values())} B, generation {generation}, CRC verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
