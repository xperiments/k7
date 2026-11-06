"""A snapshot image, built the way the host producer builds one.

Shared by the tests that need an image rather than a reader: the format is a contract,
and a test that invents its own bytes is testing itself.

This is format version 3 only (docs/specs/snapshot-format.md): an 80-byte header, 32-byte
rows sorted by fingerprint, an interned string pool split at `row_pool_size`, then the
list records and list rows.
"""
import struct
import zlib

# The header's magic field is eight bytes: K7SNAP and two NULs (format spec §3).
MAGIC = b"K7SNAP\0\0"

HEADER_SIZE = 80
ROW_SIZE = 32
LIST_SIZE = 32
LIST_ROW_SIZE = 4
UNKNOWN_PCT = 127


def fnv1a64(text):
    h = 0xCBF29CE484222325
    for byte in text.encode("utf-8"):
        h ^= byte
        h = (h * 0x00000100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def _off3(value):
    if not 0 <= value < (1 << 24):
        raise ValueError(f"string offset {value} does not fit in 24 bits")
    return struct.pack("<I", value)[:3]


def _pct(value):
    if value <= 100 or value == UNKNOWN_PCT:
        return value
    raise ValueError(f"percentage {value} is neither 0..100 nor 127")


def _measurements(r):
    """The one little-endian u64, packed from bit 0 (format spec §4)."""
    bpm_x10 = r["bpm_x10"]
    if bpm_x10 == 0:
        bpm_code = 0
    elif 700 <= bpm_x10 <= 1800:
        bpm_code = bpm_x10 - 699
    else:
        raise ValueError(f"bpm_x10 {bpm_x10} is neither 0 nor 700..1800")
    key = r["key"]
    if not 0 <= key <= 24:
        raise ValueError(f"key {key} is outside 0..24")
    word = bpm_code & 0x7FF
    word |= (key & 0x1F) << 11
    word |= (_pct(r["energy"]) & 0x7F) << 16
    word |= (_pct(r["dance"]) & 0x7F) << 23
    word |= (_pct(r["valence"]) & 0x7F) << 30
    word |= (_pct(r["arousal"]) & 0x7F) << 37
    word |= (_pct(r["conf"]) & 0x7F) << 44
    word |= (r["mood"] & 0x1F) << 51
    word |= (r["src"] & 0x3F) << 56
    return word


def build_image(rows, lists=(), generation=1767225600, analyser=300, schema=1,
                model_set=0x1234):
    """Build a version 3 image.

    rows: list of dicts with path, size, bpm_x10, key, energy, dance, valence,
    arousal, mood, conf, src, title, artist, album, genre.
    lists: list of dicts with id (int), name, created and rows (Library paths).
    Lists are sorted by id; list strings follow the row strings, each list's
    name and then its paths, in list order.
    """
    rows = sorted(rows, key=lambda r: fnv1a64(r["path"]))
    ordered_lists = sorted(lists, key=lambda l: l["id"])

    # The rows' strings, interned in first-use order over the sorted rows. The same
    # bytes are stored once, so the same offset may be named by several fields/rows.
    pool = bytearray()
    interned = {}
    encoded = []

    def intern(text):
        data = text.encode("utf-8")
        if data not in interned:
            interned[data] = len(pool)
            pool.extend(data + b"\0")
        return interned[data]

    for r in rows:
        offsets = [intern(r.get(field, "")) for field in ("title", "artist", "album", "genre")]
        encoded.append((r, offsets))
    row_pool_size = len(pool)

    name_offsets = []
    path_offsets = []
    list_rows = 0
    for playlist in ordered_lists:
        name_offsets.append(len(pool))
        pool.extend(playlist["name"].encode("utf-8") + b"\0")
        offsets = []
        for path in playlist["rows"]:
            offsets.append(len(pool))
            pool.extend(path.encode("utf-8") + b"\0")
        path_offsets.append(offsets)
        list_rows += len(playlist["rows"])

    body = bytearray()
    for r, offsets in encoded:
        body += struct.pack("<QI", fnv1a64(r["path"]), r["size"])
        for offset in offsets:
            body += _off3(offset)
        body += struct.pack("<Q", _measurements(r))
    body += pool

    row_at = 0
    for index, playlist in enumerate(ordered_lists):
        body += struct.pack(
            "<QIIIQ4s",
            playlist["id"], name_offsets[index], row_at, len(playlist["rows"]),
            playlist.get("created", 0), b"\0" * 4,
        )
        row_at += len(playlist["rows"])
    for offsets in path_offsets:
        for offset in offsets:
            body += struct.pack("<I", offset)

    total = HEADER_SIZE + len(body)
    header = struct.pack(
        "<8sHHIIIQIIIIQQ",
        MAGIC, 3, HEADER_SIZE, total, len(rows), ROW_SIZE, generation, len(pool),
        zlib.crc32(bytes(body)) & 0xFFFFFFFF, analyser, schema, model_set, 0,
    )
    header += struct.pack(
        "<HHHHII", len(ordered_lists), LIST_SIZE, LIST_ROW_SIZE, 0, list_rows,
        row_pool_size,
    )
    assert len(header) == HEADER_SIZE, len(header)
    return header + bytes(body)
