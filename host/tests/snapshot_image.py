"""A snapshot image, built the way the host producer builds one.

Shared by the tests that need an image rather than a reader: the format is a contract,
and a test that invents its own bytes is testing itself.
"""
import struct
import zlib

MAGIC = b"XTAPESNP"


def fnv1a64(text):
    h = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        h ^= byte
        h = (h * 0x00000100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def build_image(rows, generation=1767225600, analyser=300, schema=1, model_set=0x1234):
    """rows: list of dicts with path, size, bpm_x10, key, energy, dance, valence,
    arousal, mood, conf, src, title, artist, album, genre."""
    rows = sorted(rows, key=lambda r: fnv1a64(r["path"]))
    pool = bytearray()
    encoded = []
    for r in rows:
        offset = len(pool)
        for field in ("title", "artist", "album", "genre"):
            pool += r.get(field, "").encode("utf-8") + b"\0"
        encoded.append((r, offset))

    body = bytearray()
    for r, offset in encoded:
        body += struct.pack(
            "<QIIHBBBBBBBB6s",
            fnv1a64(r["path"]), r["size"], offset, r["bpm_x10"], r["key"],
            r["energy"], r["dance"], r["valence"], r["arousal"], r["mood"],
            r["conf"], r["src"], b"\0" * 6,
        )
    body += pool
    total = 64 + len(body)
    header = struct.pack(
        "<8sHHIIIQIIIIQQ",
        MAGIC, 1, 64, total, len(rows), 32, generation, len(pool),
        zlib.crc32(bytes(body)) & 0xFFFFFFFF, analyser, schema, model_set, 0,
    )
    assert len(header) == 64, len(header)
    return header + bytes(body)




def build_image_v2(rows, lists=(), generation=1767225600, analyser=300, schema=2,
                   model_set=0x1234):
    """A version 2 image: the version 1 body plus the list area (format spec §10).

    `lists` is a list of dicts with `id` (int, non-zero), `name`, `created` and `rows`
    (Library paths). Lists are sorted by id; the pool is each row's four strings, then
    each list's name and paths, in list order.
    """
    rows = sorted(rows, key=lambda r: fnv1a64(r["path"]))
    ordered_lists = sorted(lists, key=lambda l: l["id"])

    pool = bytearray()
    encoded = []
    for r in rows:
        offset = len(pool)
        for field in ("title", "artist", "album", "genre"):
            pool += r.get(field, "").encode("utf-8") + b"\0"
        encoded.append((r, offset))

    name_offsets = []
    path_offsets = []
    list_rows = 0
    for l in ordered_lists:
        name_offsets.append(len(pool))
        pool += l["name"].encode("utf-8") + b"\0"
        offsets = []
        for path in l["rows"]:
            offsets.append(len(pool))
            pool += path.encode("utf-8") + b"\0"
        path_offsets.append(offsets)
        list_rows += len(l["rows"])

    body = bytearray()
    for r, offset in encoded:
        body += struct.pack(
            "<QIIHBBBBBBBB6s",
            fnv1a64(r["path"]), r["size"], offset, r["bpm_x10"], r["key"],
            r["energy"], r["dance"], r["valence"], r["arousal"], r["mood"],
            r["conf"], r["src"], b"\0" * 6,
        )
    body += pool

    row_at = 0
    for index, l in enumerate(ordered_lists):
        body += struct.pack(
            "<QIIIQ4s",
            l["id"], name_offsets[index], row_at, len(l["rows"]), l.get("created", 0), b"\0" * 4,
        )
        row_at += len(l["rows"])
    for index, l in enumerate(ordered_lists):
        for offset in path_offsets[index]:
            body += struct.pack("<I", offset)

    total = 80 + len(body)
    header = struct.pack(
        "<8sHHIIIQIIIIQQ",
        MAGIC, 2, 80, total, len(rows), 32, generation, len(pool),
        zlib.crc32(bytes(body)) & 0xFFFFFFFF, analyser, schema, model_set, 0,
    )
    header += struct.pack("<HHHHII", len(ordered_lists), 32, 4, 0, list_rows, 0)
    assert len(header) == 80, len(header)
    return header + bytes(body)
