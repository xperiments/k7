#!/usr/bin/env python3
"""Compare GET /api/library with the fixture tree it was built from.

The JSON is the library's own prebuilt view; this script is the independent
oracle. It walks the directory with os.walk() and compares every indexed track's
path and size, plus the counts and total bytes. The state modes are for the
boot-time no-card, missing-directory and empty cases, where there is nothing to
walk.
"""
import argparse
import json
import os
import sys

AUDIO = {".mp3", ".flac", ".wav"}


def fixture_tracks(root):
    tracks = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(
            d for d in dirnames
            if not d.startswith(".") and not os.path.islink(os.path.join(dirpath, d))
        )
        for name in sorted(filenames):
            if (name.startswith(".") or ".." in name or "/" in name or "\\" in name
                    or len(name) > 128 or os.path.islink(os.path.join(dirpath, name))):
                continue
            if os.path.splitext(name)[1].lower() not in AUDIO:
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            tracks[rel] = os.path.getsize(full)
    return tracks


def ascii_key(name):
    folded = "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in name)
    return (folded, name)


def check_tree(tree):
    """Validate parentage, paths and per-directory playlist order."""
    tracks = {}
    seen_dirs = 0

    def walk(node, parent_path, is_root=False):
        nonlocal seen_dirs
        if node.get("type") not in ("dir", "track"):
            raise ValueError(f"bad node type: {node.get('type')!r}")
        expected = "" if is_root else (
            node["name"] if not parent_path else parent_path + "/" + node["name"])
        if node["path"] != expected:
            raise ValueError(f"path {node['path']!r}, expected {expected!r}")
        if node["type"] == "track":
            tracks[node["path"]] = node["size"]
            return
        seen_dirs += 1
        children = node.get("children", [])
        names = [c["name"] for c in children]
        if names != sorted(names, key=ascii_key):
            raise ValueError(f"children of {node['path']!r} are not in playlist order")
        for child in children:
            walk(child, node["path"])

    walk(tree, "", True)
    return tracks, seen_dirs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("json_file")
    ap.add_argument("root")
    ap.add_argument("--state", default="ok",
                    choices=["ok", "empty", "no_card", "no_dir"])
    ap.add_argument("--truncated", action="store_true")
    args = ap.parse_args()

    with open(args.json_file, encoding="utf-8") as f:
        doc = json.load(f)
    for key in ("root", "generation", "count", "bytes", "state", "truncated", "error", "tree"):
        if key not in doc:
            print(f"missing top-level key {key!r}")
            return 1
    if not isinstance(doc["generation"], int):
        print("generation is not an integer")
        return 1
    if doc["state"] != args.state:
        print(f"state is {doc['state']!r}, want {args.state!r}")
        return 1
    if bool(doc["truncated"]) != args.truncated:
        print(f"truncated is {doc['truncated']!r}, want {args.truncated}")
        return 1

    try:
        got, dirs = check_tree(doc["tree"])
    except ValueError as e:
        print(e)
        return 1
    if args.state in ("no_card", "no_dir", "empty"):
        if args.truncated:
            print(f"state {args.state} cannot be truncated")
            return 1
        if doc["count"] != 0 or doc["bytes"] != 0 or got:
            print("a non-ok state must have no tracks and no bytes")
            return 1
        if args.state in ("no_card", "no_dir") and not doc["error"]:
            print(f"state {args.state} has no diagnosis")
            return 1
        print(f"library JSON state {args.state}: empty tree")
        return 0

    want = fixture_tracks(args.root)
    if doc["count"] != len(got):
        print(f"count is {doc['count']}, the JSON tree has {len(got)} tracks")
        return 1
    if doc["bytes"] != sum(got.values()):
        print(f"bytes is {doc['bytes']}, the JSON tree sums to {sum(got.values())}")
        return 1
    if not args.truncated:
        missing = sorted(set(want) - set(got))
        extra = sorted(set(got) - set(want))
        if missing:
            print(f"os.walk() tracks absent from JSON: {missing[:3]}")
            return 1
        if extra:
            print(f"JSON tracks absent from os.walk(): {extra[:3]}")
            return 1
        if dirs != 1 + sum(len(dn) for _, dn, _ in os.walk(args.root)):
            print(f"directory count is {dirs}, os.walk() has {1 + sum(len(dn) for _, dn, _ in os.walk(args.root))}")
            return 1
    elif len(got) >= len(want):
        print(f"a truncated JSON has {len(got)} of {len(want)} tracks; it cannot have them all")
        return 1
    for path, size in got.items():
        if path not in want:
            print(f"JSON track not found by os.walk(): {path}")
            return 1
        if want[path] != size:
            print(f"{path}: JSON says {size}, file is {want[path]}")
            return 1
    scope = "every indexed track" if args.truncated else "all tracks"
    print(f"library JSON matches os.walk(): {len(got)}/{len(want)} {scope}, "
          f"{sum(got.values())} B, {dirs} dir(s), generation {doc['generation']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
