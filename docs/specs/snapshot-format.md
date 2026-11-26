# Snapshot format

**Status:** accepted · **Updated:** 2026-11-16

The byte layout of a **Snapshot** — the file the host writes to the card as `k7.db` and the
device flashes into its `snapshot` partition. Two implementations must agree on every byte: the
browser producer (`web/`) and the firmware reader (`components/portable/snapshot/`). The
decision to keep the measurements in a flash partition is ADR 0047; the decision to read it from
PSRAM and to intern its strings is ADR 0069. This document is the contract both need.

Everything here is normative unless marked **[producer]** (a rule only the host can enforce) or
**[reader]** (a rule only the device can enforce).

## 1. Conventions

- Little-endian throughout, at every width, with no exception for strings.
- Fixed-width fields; no varints, no structure padding, no native pointers.
- Percentages are `0..100`, with **`127` meaning unknown** (the C API still exposes unknown as
  `0xFF`; the reader maps 127 to it).
- BPM is BPM × 10, folded to `700..1800`, encoded as `bpm_x10 - 699` in `1..1101`; `0` means
  unknown.
- A track's length is whole milliseconds in `0..4294967295`; `0` means unknown. The value is
  the decoded duration rounded half away from zero, and is never interpreted as a file size.
- Key is `0` unknown or `1..24`; mood bits and source flags are bit fields.
- "Unknown" has exactly one representation per field. A field is never absent — a missing
  measurement is an unknown value, not a shorter record.
- Offsets and lengths are unsigned and validated by the reader before use.

**This is format version 5 only.** Versions 1 to 4, and the `XTAPESNP` magic that preceded the
rename, are refused. A v5 reader accepts nothing else (ADR 0087); the producer writes nothing
else. Version 5 removed the Playlist list area: the **Playlists** are files on the card now
([`playlist-format.md`](./playlist-format.md), [ADR 0095](../adr/0095-a-playlist-is-a-file-on-its-card.md)).

## 2. Image layout

```text
+------------------------------------------------------------------+
| header        80 B   magic, version, sizes, provenance, CRC      |
| rows          row_count x 36 B, strictly ascending by fingerprint|
| string pool   pool_size B, UTF-8, NUL-terminated, interned       |
+------------------------------------------------------------------+
```

`total_size = 80 + row_count * 36 + pool_size`. That equality is checked, not assumed. There is no
padding between sections and no second copy.

The string pool holds the rows' strings, interned: one copy of every distinct string, pointed at
by the rows' four offsets. The same string may be shared by any number of rows and by more than
one field of one row. Nothing else is in the pool: a Playlist is a file of its own
([`playlist-format.md`](./playlist-format.md)).

The image is written at offset 0 of the `snapshot` partition. The file's length **is**
`total_size`.

## 3. Header (80 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `magic` | ASCII `K7SNAP`, NUL-padded to 8 bytes |
| 8 | 2 | `format_version` | `5` |
| 10 | 2 | `header_size` | `80` |
| 12 | 4 | `total_size` | image length in bytes, header included |
| 16 | 4 | `row_count` | number of rows |
| 20 | 4 | `row_size` | `36` |
| 24 | 8 | `generation` | Unix seconds (UTC) when the host built the image |
| 32 | 4 | `pool_size` | length of the whole string pool in bytes |
| 36 | 4 | `crc32` | CRC-32/IEEE of bytes `[80, total_size)` |
| 40 | 4 | `analyser` | analyser version, `major*10000 + minor*100 + patch` |
| 44 | 4 | `app_schema` | the `ANALYSIS_SCHEMA` the producer used |
| 48 | 8 | `model_set` | FNV-1a 64 over the sorted model descriptors |
| 56 | 24 | `reserved` | zero |

`crc32` covers **everything after the header** — the rows and the string pool — and nothing in the
header itself. Bytes 64..80 held the list area's counts in versions up to 4; in version 5 they are
reserved and zero.

`generation` is informational. `model_set` is the producer's identity for the exact model set
used: FNV-1a 64 over the sorted, newline-joined descriptors (model name and content hash), no
trailing newline.

## 4. Row (36 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `fingerprint` | FNV-1a 64 of the relative path (§6) |
| 8 | 4 | `duration_ms` | the decoded length in whole milliseconds; `0` unknown |
| 12 | 4 | `size` | file size in bytes, as the filesystem reports it |
| 16 | 3 | `title_offset` | offset into the pool of the title |
| 19 | 3 | `artist_offset` | offset of the artist |
| 22 | 3 | `album_offset` | offset of the album |
| 25 | 3 | `genre_offset` | offset of the genre |
| 28 | 8 | `measurements` | the packed word below |

All four offsets are relative to the start of the pool, and each points at one NUL-terminated
string. Offsets are 24-bit, so the whole pool is at most 16 MiB. `duration_ms` is informational:
it is not part of the binding and never changes whether a row matches.

`measurements` is one little-endian `uint64`, packed from the least significant bit:

| Bits | Field | Value |
|---:|---|---|
| 0..10 | `bpm_code` | `0` unknown, otherwise `bpm_x10 - 699` in `1..1101` |
| 11..15 | `key` | `0` unknown, otherwise `1..24` |
| 16..22 | `energy` | `0..100`, or `127` unknown |
| 23..29 | `danceability` | `0..100`, or `127` unknown |
| 30..36 | `valence` | `0..100`, or `127` unknown |
| 37..43 | `arousal` | `0..100`, or `127` unknown |
| 44..50 | `confidence` | `0..100`, or `127` unknown |
| 51..55 | `mood_bits` | §5.3 |
| 56..61 | `source_flags` | §5.4 |
| 62..63 | `reserved` | zero |

Rows are sorted by `fingerprint`, strictly ascending. Duplicates are invalid: the producer must
detect a collision and refuse to write, and the reader rejects an image that is not ascending.
The sort is what makes the reader's lookup a binary search with no index to build.

`size` is part of the binding, not decoration: a row only describes a track whose size still
matches. A file replaced under the same name with a different size reads as **unanalysed**.

## 5. Encodings

### 5.1 BPM

The **folded** BPM the analyser presents — the value that was moved into 70–180 by doubling or
halving — multiplied by ten and rounded half away from zero: `124.5 → 1245`, stored as
`1245 - 699 = 546`. The detector's raw value before folding is not stored.

### 5.2 Key (Camelot)

| Value | Key | Value | Key | Value | Key | Value | Key |
|---:|---|---:|---|---:|---|---:|---|
| 1 | 1A | 7 | 7A | 13 | 1B | 19 | 7B |
| 2 | 2A | 8 | 8A | 14 | 2B | 20 | 8B |
| 3 | 3A | 9 | 9A | 15 | 3B | 21 | 9B |
| 4 | 4A | 10 | 10A | 16 | 4B | 22 | 10B |
| 5 | 5A | 11 | 11A | 17 | 5B | 23 | 11B |
| 6 | 6A | 12 | 12A | 18 | 6B | 24 | 12B |

So `8A = 8`, `8B = 20`, `12A = 12`, `12B = 24`.

### 5.3 `mood_bits`

| Bit | Mask | Mood |
|---:|---:|---|
| 0 | `0x01` | happy |
| 1 | `0x02` | sad |
| 2 | `0x04` | aggressive |
| 3 | `0x08` | relaxed |
| 4 | `0x10` | party |

A bit is set when the corresponding model head scored at or above 0.5, or when the track's own
tagged mood maps onto that head. **Danceability is not here** — it has its own field.

### 5.4 `source_flags`

| Bit | Mask | Meaning |
|---:|---:|---|
| 0 | `0x01` | BPM came from a tag, not the detector |
| 1 | `0x02` | key came from a tag |
| 2 | `0x04` | mood came from a tag |
| 3 | `0x08` | energy came from a tag |
| 4 | `0x10` | valence and arousal are present (the DEAM model ran) |
| 5 | `0x20` | the BPM detector escalated fast → deep |

Bits 6–7 are zero. The flags never change a value, only how it should be described.

## 6. Fingerprint and relative paths

```text
FNV-1a, 64-bit
offset basis 0xCBF29CE484222325
prime        0x00000100000001B3
input        the UTF-8 bytes of the relative path, exactly
```

The relative path is relative to the Music directory, `/` separators, no leading or trailing
slash, no NUL, in the bytes the filesystem returned: `Artist/Album/01 - Track.mp3`.

The producer selects the **card root**, finds its `music` child (matched without regard to case,
because the card is FAT32), and makes every path relative to it. A selection without a `music`
directory is refused rather than written. Case and Unicode are not normalised below that
segment; both sides hash the bytes they were given.

## 7. String pool

The pool is a table of NUL-terminated UTF-8 strings. Each row names four of them by offset.
**[producer]** The producer interns exact byte strings: the same title, artist, album or genre
is stored once and shared. The reader does not rely on sharing, but it validates every offset.

**[producer]** Keep each string under 1 KB and the pool under 1 MB. The hard limit is the 24-bit
offset, 16 MiB.

**[reader]** Every row's four offsets must be `< pool_size`, and each string must terminate inside
`[0, pool_size)`.

## 8. Lists

There are none. A **Playlist** is a file on the card, one per list, at
`k7.playlists/<id>.k7list`; its format is [`playlist-format.md`](./playlist-format.md). Version 5
of this image removed the list area that versions 2 to 4 carried, so nothing about a list is in an
image and an image says nothing about what will play next.

## 9. Validation

**[reader]** An image is usable only if all of these hold. Any failure means "no Snapshot".

1. `total_size >= 80` and `total_size <= partition size`.
2. `magic` is `K7SNAP`, `format_version == 5`, `header_size == 80`, `row_size == 36`.
3. Header reserved bytes (`56..63` and `64..79`) are zero.
4. `total_size == 80 + row_count*36 + pool_size`, with no 32-bit overflow in any
   multiplication.
5. `crc32` matches `[80, total_size)`.
6. Rows are strictly ascending by `fingerprint`.
7. Each of a row's four offsets is `< pool_size`, and its string terminates inside
   `[0, pool_size)`.
8. `bpm_code <= 1101`; `key <= 24`; each percentage is `<= 100` or `== 127`; `mood_bits <= 0x1F`;
   `source_flags <= 0x3F`; the measurement word's bits 62–63 are zero. `duration_ms` is any
   `uint32`; `0` is the only unknown, and no range is refused.

## 10. Determinism

**[producer]** The same input must produce the same bytes: rows sorted by fingerprint, the row
strings in first-use order over those sorted rows (title, artist, album, genre for each row),
reserved fields zeroed, percentages rounded half away from zero. Two runs over an unchanged library produce byte-identical files.

## 11. Size

Rows are fixed at 36 B; the pool is one copy of each distinct string.

| Tracks | Rows | Pool (est., interned) | Image |
|---:|---:|---:|---:|
| 1,000 | 36 KB | ~25 KB | ~61 KB |
| 5,000 | 180 KB | ~100 KB | ~280 KB |
| 20,000 | 720 KB | ~350 KB | ~1.07 MB |

The 5,000-track image fits the 2 MB partition with room for a second axis of growth; at 20,000
the interned format still fits, which the run-level v2 pool did not.

## 12. Versioning

`format_version = 5` is the only version this project reads or writes. There is no compatibility
shim for versions 1 to 4 and no legacy magic; an unknown version is refused, and the device serves
without analysis until a v5 image is applied. A Snapshot is cheap to regenerate, so there is no
reason to guess at a half-understood image. The move from 4 to 5 removed the list area and nothing
else: a v4 image's rows mean what a v5 image's rows mean.
