# Snapshot format

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
- Key is `0` unknown or `1..24`; mood bits and source flags are bit fields.
- "Unknown" has exactly one representation per field. A field is never absent — a missing
  measurement is an unknown value, not a shorter record.
- Offsets and lengths are unsigned and validated by the reader before use.

**This is format version 3 only.** Versions 1 and 2, and the `XTAPESNP` magic that preceded the
rename, are refused. A v3 reader accepts nothing else (ADR 0069); the producer writes nothing
else.

## 2. Image layout

```text
+------------------------------------------------------------------+
| header        80 B   magic, version, sizes, provenance, CRC      |
| rows          row_count x 32 B, strictly ascending by fingerprint|
| string pool   pool_size B, UTF-8, NUL-terminated, interned       |
| lists         list_count x 32 B, strictly ascending by list id   |
| list rows     list_rows x 4 B, tiled by the lists in list order  |
+------------------------------------------------------------------+
```

`total_size = 80 + row_count * 32 + pool_size + list_count * 32 + list_rows * 4`. That equality
is checked, not assumed. There is no padding between sections and no second copy.

The string pool has two regions:

- `[0, row_pool_size)` — the rows' strings, interned: one copy of every distinct string, pointed
  at by the rows' four offsets. The same string may be shared by any number of rows and by more
  than one field of one row.
- `[row_pool_size, pool_size)` — the list strings: each list's name and then its rows' paths, in
  list order. A list string need not be shared with a row string.

`row_pool_size` is stored in the header, so a producer can replace the list area without knowing
the row dictionary (`patchListArea`) and the reader can validate each region separately.

The image is written at offset 0 of the `snapshot` partition. The file's length **is**
`total_size`.

## 3. Header (80 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `magic` | ASCII `K7SNAP`, NUL-padded to 8 bytes |
| 8 | 2 | `format_version` | `3` |
| 10 | 2 | `header_size` | `80` |
| 12 | 4 | `total_size` | image length in bytes, header included |
| 16 | 4 | `row_count` | number of rows |
| 20 | 4 | `row_size` | `32` |
| 24 | 8 | `generation` | Unix seconds (UTC) when the host built the image |
| 32 | 4 | `pool_size` | length of the whole string pool in bytes |
| 36 | 4 | `crc32` | CRC-32/IEEE of bytes `[80, total_size)` |
| 40 | 4 | `analyser` | analyser version, `major*10000 + minor*100 + patch` |
| 44 | 4 | `app_schema` | the `ANALYSIS_SCHEMA` the producer used |
| 48 | 8 | `model_set` | FNV-1a 64 over the sorted model descriptors |
| 56 | 8 | `reserved` | zero |
| 64 | 2 | `list_count` | number of Playlists, `0..64` |
| 66 | 2 | `list_size` | `32` |
| 68 | 2 | `list_row_size` | `4` |
| 70 | 2 | `reserved` | zero |
| 72 | 4 | `list_rows` | Playlist rows across all lists, `0..8192` |
| 76 | 4 | `row_pool_size` | bytes of the pool occupied by row strings, `<= pool_size` |

`crc32` covers **everything after the header** — rows, both pool regions, list records and list
rows — and nothing in the header itself.

`generation` is informational. `model_set` is the producer's identity for the exact model set
used: FNV-1a 64 over the sorted, newline-joined descriptors (model name and content hash), no
trailing newline.

## 4. Row (32 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `fingerprint` | FNV-1a 64 of the relative path (§6) |
| 8 | 4 | `size` | file size in bytes, as the filesystem reports it |
| 12 | 3 | `title_offset` | offset into the pool of the title |
| 15 | 3 | `artist_offset` | offset of the artist |
| 18 | 3 | `album_offset` | offset of the album |
| 21 | 3 | `genre_offset` | offset of the genre |
| 24 | 8 | `measurements` | the packed word below |

All four offsets are relative to the start of the pool, and each points at one NUL-terminated
string. Offsets are 24-bit, so the whole pool is at most 16 MiB.

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

The first `row_pool_size` bytes are the rows' strings; the rest are the lists' strings, in list
order: each list's name, then its rows' paths.

**[producer]** Keep each string under 1 KB, a list's name under 64 bytes, and the pool under
1 MB. The hard limit is the 24-bit offset, 16 MiB.

**[reader]** Every row's four offsets must be `< row_pool_size`, and each string must terminate
inside `[0, row_pool_size)`. Every list name and path offset must be in
`[row_pool_size, pool_size)` and terminate inside it.

## 8. Lists

A **Playlist** is an ordered selection of the Library's Tracks, stored here because this image is
the one artifact the host produces for the device (ADR 0051).

Limits: `list_count` 0..64, `list_rows` 0..8192 across the image, a list's name under 64 bytes,
and a list's own `row_count` may not exceed `list_rows`. A producer that breaks a limit refuses
to write rather than truncating. A list may have **no rows**; selecting one is the command's
`409`, not an invalid image.

### List record (32 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `list_id` | FNV-1a 64 of the list's name and creation time, strictly ascending |
| 8 | 4 | `name_offset` | offset into the list-string region of the NUL-terminated name |
| 12 | 4 | `row_start` | index of the list's first row in the list-row array |
| 16 | 4 | `row_count` | number of rows in this list |
| 20 | 8 | `created` | Unix seconds (UTC) when the list was made |
| 28 | 4 | `reserved` | zero |

`list_id` is opaque to the device: it is what `select` and the card's selection record carry, so
renaming a list must not change it. Two lists in one image may not share an id.

### List row (4 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 4 | `path_offset` | offset into the list-string region of the Track's relative path |

A row names a Track by the Library's own key: its relative path. The device resolves it with
`library_find`, the same lookup `/api/play` uses. A path the Library does not hold is **skipped**
at playback and reported as missing; it is never deleted. A list row deliberately does not bind
the file's size or content.

## 9. Validation

**[reader]** An image is usable only if all of these hold. Any failure means "no Snapshot".

1. `total_size >= 80` and `total_size <= partition size`.
2. `magic` is `K7SNAP`, `format_version == 3`, `header_size == 80`, `row_size == 32`.
3. Header reserved bytes (`56..63`, `70..71`) are zero.
4. `row_pool_size <= pool_size`.
5. `total_size == 80 + row_count*32 + pool_size + list_count*32 + list_rows*4`, with no
   32-bit overflow in any multiplication.
6. `crc32` matches `[80, total_size)`.
7. Rows are strictly ascending by `fingerprint`.
8. Each of a row's four offsets is `< row_pool_size`, and its string terminates inside
   `[0, row_pool_size)`.
9. `bpm_code <= 1101`; `key <= 24`; each percentage is `<= 100` or `== 127`; `mood_bits <= 0x1F`;
   `source_flags <= 0x3F`; the measurement word's bits 62–63 are zero.
10. `list_count <= 64`, `list_rows <= 8192`, `list_size == 32`, `list_row_size == 4`; list ids
    strictly ascending; each list's name and paths terminate in
    `[row_pool_size, pool_size)`; the lists tile the list-row array exactly (the first starts at
    0, each `row_start` is the previous one plus its `row_count`, and the total equals
    `list_rows`).

## 10. Determinism

**[producer]** The same input must produce the same bytes: rows sorted by fingerprint, lists
sorted by id, the row strings in first-use order over those sorted rows (title, artist, album,
genre for each row), then the list strings in list order, reserved fields zeroed, percentages
rounded half away from zero. Two runs over an unchanged library produce byte-identical files.

## 11. Size

Rows are fixed at 32 B; the pool is one copy of each distinct string.

| Tracks | Rows | Pool (est., interned) | Image |
|---:|---:|---:|---:|
| 1,000 | 32 KB | ~25 KB | ~57 KB |
| 5,000 | 160 KB | ~100 KB | ~260 KB |
| 20,000 | 640 KB | ~350 KB | ~1 MB |

The 5,000-track image fits the 2 MB partition with room for a second axis of growth; at 20,000
the interned format still fits, which the run-level v2 pool did not.

## 12. Versioning

`format_version = 3` is the only version this project reads or writes. There is no compatibility
shim for v1 or v2 and no legacy magic; an unknown version is refused, and the device serves
without analysis until a v3 image is applied. A Snapshot is cheap to regenerate, so there is no
reason to guess at a half-understood image.
