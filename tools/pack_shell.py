#!/usr/bin/env python3
"""Pack the Shell into the one image the device maps.

    pack_shell.py DIRECTORY OUTPUT.bin [--quiet]

The front door serves `web/dist-shell`. On the board those bytes live in the `frontdoor`
flash partition and are read out of it ONCE at boot, into PSRAM: a read through the partition
API disables the cache and the PSRAM window with it, and even a mapped read is a cache line
fetch on the bus the panel scans from - which is what tore the picture every time a page
loaded (ADR 0055). So the image is copied while nothing is scanning, and the page is served
from RAM.

So the partition is not a filesystem any more. It is this image: a header, a table of
contents, the names, and the files' bytes exactly as they sit on disk - the text files
already gzipped by the build, the rest plain. Nothing on the device decompresses anything;
`Content-Encoding: gzip` is just a header on bytes that are already compressed.

Format, little-endian, every offset absolute from the start of the image:

    struct header {                     /* 24 bytes, at 0 */
        char     magic[4];              /* "XTSP" */
        uint32_t version;               /* 1 */
        uint32_t count;
        uint32_t names_off;             /* the NUL-terminated names */
        uint32_t data_off;              /* the first blob */
        uint32_t size;                  /* the image's own length in bytes */
    };
    struct entry {                      /* 16 bytes each, count of them at 24 */
        uint32_t name_off;              /* NUL-terminated, no leading '/' */
        uint16_t name_len;
        uint16_t flags;                 /* bit 0: the stored bytes are gzip */
        uint32_t data_off;
        uint32_t data_len;
    };

Entries are sorted by name, blobs are 4-byte aligned, and the image never changes once
written: the firmware only reads it.
"""
import argparse
import os
import struct
import sys

MAGIC = b"XTSP"
VERSION = 1
FLAG_GZIP = 1 << 0
HEADER = struct.Struct("<4sIIIII")
ENTRY = struct.Struct("<IHHII")
ALIGN = 4


def align(n):
    return (n + ALIGN - 1) & ~(ALIGN - 1)


def walk(root):
    """Every file under root, as (logical path without leading '/', stored path, gzip?)."""
    found = {}
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            stored = os.path.join(dirpath, name)
            logical = "/" + os.path.relpath(stored, root).replace(os.sep, "/")
            gzip = logical.endswith(".gz")
            if gzip:
                logical = logical[:-3]
            # The build drops the plain copy of anything it packs, so a collision is not
            # expected; if one ever appears the packed copy wins, which is the order the
            # directory-serving front door used to try them in.
            if logical in found and not gzip:
                continue
            found[logical] = (stored, gzip)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("directory")
    ap.add_argument("output")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    root = os.path.abspath(args.directory)
    if not os.path.isdir(root):
        sys.exit(f"pack_shell: not a directory: {root}")

    files = walk(root)
    if not files:
        sys.exit(f"pack_shell: no files under {root}")

    names = sorted(files)
    count = len(names)
    entries_off = HEADER.size
    names_off = entries_off + count * ENTRY.size

    names_blob = b""
    name_offsets = {}
    for logical in names:
        name_offsets[logical] = names_off + len(names_blob)
        names_blob += logical.lstrip("/").encode("utf-8") + b"\0"

    data_cursor = align(names_off + len(names_blob))
    # The names end mid-word as often as not, and the first blob is 4-byte aligned: the
    # filler between them is part of the image and has to be written with it.
    names_blob += b"\0" * (data_cursor - names_off - len(names_blob))
    first_data = data_cursor

    entries = []
    blobs = []
    payload = 0
    for logical in names:
        stored, gzip = files[logical]
        with open(stored, "rb") as f:
            body = f.read()
        offset = align(data_cursor)
        if offset != data_cursor:
            blobs.append(b"\0" * (offset - data_cursor))
        name = logical.lstrip("/").encode("utf-8")
        entries.append(ENTRY.pack(name_offsets[logical], len(name), FLAG_GZIP if gzip else 0,
                                  offset, len(body)))
        blobs.append(body)
        data_cursor = offset + len(body)
        payload += len(body)

    # The header states the image's own length as well as where its parts start: the device
    # maps a partition that is larger than the image, so "how many bytes were you given" is
    # not the bound a lookup may trust. `data_cursor` is now the end of the last blob.
    head = HEADER.pack(MAGIC, VERSION, count, names_off, first_data, data_cursor)

    with open(args.output, "wb") as out:
        out.write(head)
        out.write(b"".join(entries))
        out.write(names_blob)
        for blob in blobs:
            out.write(blob)

    size = os.path.getsize(args.output)
    if not args.quiet:
        packed = sum(1 for logical in names if files[logical][1])
        print(f"shell image: {count} files ({packed} gzip), {payload} B of bytes -> "
              f"{size} B at {args.output}")


if __name__ == "__main__":
    main()
