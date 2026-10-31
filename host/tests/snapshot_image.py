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


