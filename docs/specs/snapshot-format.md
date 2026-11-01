# Snapshot format

The byte layout of a **Snapshot** — the file the host writes to the card as `xtape.db` and the
device flashes into its `snapshot` partition. Two implementations must agree on every byte: the
browser producer (`web/`) and the firmware reader (`components/portable/snapshot/`). The
decision to keep the measurements in a flash partition is ADR 0047; this document is the
contract that decision needs.

Everything here is normative unless marked **[producer]** (a rule only the host can enforce) or
**[reader]** (a rule only the device can enforce).

## 1. Conventions

- Little-endian throughout, at every width, with no exception for strings.
- Fixed-width fields; no varints, no structure padding, no native pointers.
- All values are **percent** (`0..100`) except BPM, which is BPM × 10, and the fingerprint.
- "Unknown" has exactly one representation per field: `0` for BPM and key, `0xFF` for a
  percentage, `0` bits for mood. A field is never absent — a missing measurement is an unknown
  value, not a shorter record.
- Offsets and lengths are unsigned and validated by the reader before use.

## 2. Image layout

Version 1:

```text
+------------------------------------------------------------------+
| header      64 B   magic, version, sizes, provenance, CRC       |
| rows        row_count x 32 B, strictly ascending by fingerprint |
| strings     pool_size B, UTF-8, NUL-terminated                  |
+------------------------------------------------------------------+
```

`total_size = 64 + row_count * 32 + pool_size`. That equality is checked, not assumed. There are
no other sections, no directory, and no alignment padding between them.

Version 2 adds one area, after everything version 1 has:

```text
+------------------------------------------------------------------+
| header      80 B   as version 1, then the list counts            |
| rows        row_count x 32 B, strictly ascending by fingerprint |
| strings     pool_size B: row strings, then list names and paths  |
| lists       list_count x 32 B, strictly ascending by list id     |
| list rows   list_rows x 12 B, tiled by the lists in list order   |
+------------------------------------------------------------------+
```

`total_size = 80 + row_count * 32 + pool_size + list_count * 32 + list_rows * 12`. The list area is
absent when `list_count` is zero, and an image with nothing to say about lists is still written as
version 1 (§12).

The image is written at offset 0 of the `snapshot` partition. The file's length **is**
`total_size`; a longer file is a longer image, not a padded one.

## 3. Header (64 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `magic` | ASCII `XTAPESNP` |
| 8 | 2 | `format_version` | `1` |
| 10 | 2 | `header_size` | `64` |
| 12 | 4 | `total_size` | image length in bytes, header included |
| 16 | 4 | `row_count` | number of rows |
| 20 | 4 | `row_size` | `32` |
| 24 | 8 | `generation` | Unix seconds (UTC) when the host built the image |
| 32 | 4 | `pool_size` | length of the string pool in bytes |
| 36 | 4 | `crc32` | CRC-32/IEEE of bytes `[64, total_size)` |
| 40 | 4 | `analyser` | analyser version, `major*10000 + minor*100 + patch` |
| 44 | 4 | `app_schema` | the `ANALYSIS_SCHEMA` the producer used |
| 48 | 8 | `model_set` | FNV-1a 64 over the sorted model descriptors |
| 56 | 8 | `reserved` | zero |

`crc32` covers **everything after the header** — rows and pool — and nothing in the header
itself. There is no header checksum: a corrupt header fails one of the structural checks
(magic, sizes, version) or makes `total_size` disagree with the fields, which is why those
checks exist.

`generation` is informational. The device reports it and never compares it to anything; the
card's possession of an Update file is the only "should I update" signal (ADR 0047).

`model_set` is the producer's identity for the exact model set used: FNV-1a 64 over the sorted,
newline-joined descriptors (model name and content hash), no trailing newline. It exists so the
device can report *which* models produced a Snapshot without carrying a string table.

### 3.1 Version 2 header (80 bytes)

Bytes `0..64` are exactly the version 1 header, with `format_version = 2` and
`header_size = 80`. Bytes `64..80` are:

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 64 | 2 | `list_count` | number of Playlists, `0..64` |
| 66 | 2 | `list_size` | `32` |
| 68 | 2 | `list_row_size` | `12` |
| 70 | 2 | `reserved` | zero |
| 72 | 4 | `list_rows` | Playlist rows across all lists, `0..8192` |
| 76 | 4 | `reserved` | zero |

`crc32` in a version 2 image covers `[80, total_size)`: everything after the header, as in version 1,
with the header now being 80 bytes. `header_size` is what tells the reader which length to use, and
it is checked before any other offset is read.

## 4. Row (32 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `fingerprint` | FNV-1a 64 of the relative path (§6) |
| 8 | 4 | `size` | file size in bytes, as the filesystem reports it |
| 12 | 4 | `string_offset` | offset into the pool of the row's four strings |
| 16 | 2 | `bpm_x10` | `700..1800`, or `0` for unknown |
| 18 | 1 | `key` | `0` unknown, or `1..24` (§5.2) |
| 19 | 1 | `energy` | `0..100`, or `0xFF` |
| 20 | 1 | `danceability` | `0..100`, or `0xFF` |
| 21 | 1 | `valence` | `0..100`, or `0xFF` |
| 22 | 1 | `arousal` | `0..100`, or `0xFF` |
| 23 | 1 | `mood_bits` | §5.3 |
| 24 | 1 | `confidence` | `0..100`, or `0xFF` |
| 25 | 1 | `source_flags` | §5.4 |
| 26 | 6 | `reserved` | zero |

Rows are sorted by `fingerprint`, strictly ascending. Duplicates are invalid: the producer must
detect a collision and refuse to write, and the reader rejects an image that is not ascending.
The sort is what makes the reader's lookup a binary search with no index to build.

`size` is part of the binding, not decoration: a row only describes a track whose size still
matches. A file replaced under the same name with a different size reads as **unanalysed**, not
as the old measurements.

## 5. Encodings

### 5.1 BPM

The **folded** BPM the analyser presents — the value that was moved into 70–180 by doubling or
halving — multiplied by ten and rounded half away from zero: `124.5 → 1245`. The detector's raw
value before folding is not stored; it is not a second opinion the device can use.

### 5.2 Key (Camelot)

| Value | Key | Value | Key | Value | Key | Value | Key |
|---:|---|---:|---|---:|---|---:|---|
| 1 | 1A | 7 | 7A | 13 | 1B | 19 | 7B |
| 2 | 2A | 8 | 8A | 14 | 2B | 20 | 8B |
| 3 | 3A | 9 | 9A | 15 | 3B | 21 | 9B |
| 4 | 4A | 10 | 10A | 16 | 4B | 22 | 10B |
| 5 | 5A | 11 | 11A | 17 | 5B | 23 | 11B |
| 6 | 6A | 12 | 12A | 18 | 6B | 24 | 12B |

So `8A = 8`, `8B = 20`, `12A = 12`, `12B = 24`. Harmonic neighbours are arithmetic: the same
number ±1 within the letter's twelve, or the parallel at ±12. The device stores the code, not
the tonic; the note name is the producer's business.

### 5.3 `mood_bits`

| Bit | Mask | Mood |
|---:|---:|---|
| 0 | `0x01` | happy |
| 1 | `0x02` | sad |
| 2 | `0x04` | aggressive |
| 3 | `0x08` | relaxed |
| 4 | `0x10` | party |

A bit is set when the corresponding model head scored at or above 0.5, or when the track's own
tagged mood maps onto that head. **Danceability is not here** — it has its own field, because it
is a property of the body, not of the mood. `0` means "no mood known", which is a legitimate
value for a track whose model did not run and which carries no tag.

Bits 5–7 are zero and reserved.

### 5.4 `source_flags`

| Bit | Mask | Meaning |
|---:|---:|---|
| 0 | `0x01` | BPM came from a tag, not the detector |
| 1 | `0x02` | key came from a tag |
| 2 | `0x04` | mood came from a tag (a label, not the model) |
| 3 | `0x08` | energy came from a tag |
| 4 | `0x10` | valence and arousal are present (the DEAM model ran) |
| 5 | `0x20` | the BPM detector escalated fast → deep |
| 6–7 | `0xC0` | zero, reserved |

The flags never change a value, only how it should be described. A tagged BPM is presented as
the BPM; it is not presented as a measurement (ADR 0009).

## 6. Fingerprint

```text
FNV-1a, 64-bit
offset basis 0xCBF29CE484222325
prime        0x00000100000001B3
input        the UTF-8 bytes of the relative path, exactly
```

The relative path is the one the device's `library_relpath()` produces: relative to the Music
directory, `/` separators, no leading slash, no trailing slash, no NUL, in the bytes the
filesystem returned. `Artist/Album/01 - Track.mp3`, not `/sdcard/music/...`.

**[producer]** The user selects or drags **the card root**, so the browser hands over paths like
`<volume>/music/Artist/Album/track.mp3`. The Music directory is that selection's `music` child,
and every path is made relative to it before hashing: drop the volume name and the `music`
segment, leaving `Artist/Album/track.mp3`.

Resolve the Music directory **structurally** — the selection's `music` child — not by stripping
a fixed number of segments: the volume name is whatever the card was formatted with, and
`music` is fixed at `/sdcard/music` on the device. If the selection has no `music` directory,
the producer refuses with "this is not the card's root" rather than writing an image whose rows
match nothing. A wrong path costs a Snapshot with zero **Coverage**: visible, but a wasted
generation.

Case and Unicode are not normalised on either side. Both hash the bytes they were given; a
disagreement costs a missing row, never a wrong one.

## 7. String pool

The pool is the concatenation of every row's strings, **in row order**, each row contributing
four NUL-terminated UTF-8 strings in this order:

```text
title \0 artist \0 album \0 genre \0
```

An absent string is a single NUL — the four strings are always present, so a row's
`string_offset` always points at a valid title. `string_offset` is relative to the start of the
pool, and the same value may appear in more than one row (the producer may deduplicate; the
reader does not rely on it).

**[producer]** Strings carry no length limit beyond the pool's 4 GiB, but they are copied into
fixed buffers on the device, so keep each under 1 KB and the pool under 1 MB.

In a version 2 image the pool continues after the last row's strings with, in list order, each
list's name and then each of its rows' paths:

```text
name \0 path \0 path \0 ...  (one block per list, in list order)
```

**[producer]** Strings carry no length limit beyond the pool's 4 GiB, but they are copied into
fixed buffers on the device, so keep each under 1 KB, a list's name under 64 bytes, and the pool
under 1 MB.

**[reader]** Walking a row's four strings must stay inside `[pool, pool + pool_size)`; a row
whose fourth string has no terminator inside the pool invalidates the image. The same holds for a
list's name and each of its rows' paths.

## 8. Validation

**[reader]** An image is usable only if all of these hold. Any failure means "no Snapshot":
the device keeps serving, reports the reason, and never trusts a partially valid image.

1. `total_size >= 64` and `total_size <= partition size`.
2. `magic`, and `header_size` is `64` for version 1 or `80` for version 2; `row_size == 32`.
3. `format_version` is `1` or `2`. Unknown versions are refused, not guessed.
4. `total_size == header_size + row_count*32 + pool_size` (version 1), or that sum plus
   `list_count*32 + list_rows*12` (version 2), with no 32-bit overflow in any multiplication.
5. `crc32` matches `[header_size, total_size)`.
6. Rows are strictly ascending by `fingerprint`.
7. Every `string_offset < pool_size`, and the row's four strings terminate inside the pool.
8. `bpm_x10` is `0` or in `700..1800`; `key` is `0..24`; each percentage is `0..100` or `0xFF`;
   `mood_bits & 0xE0 == 0`; `source_flags & 0xC0 == 0`; `reserved` is zero.
9. Version 2 only: `list_count <= 64`, `list_rows <= 8192`, `list_size == 32`,
   `list_row_size == 12`, list ids strictly ascending, every list's name and paths terminate
   inside the pool, and the lists tile the list-row array exactly (the first starts at 0, each
   `row_start` is the previous one plus its `row_count`, and the total equals `list_rows`).

`row_count` is bounded by the partition: the reader also checks `header_size + row_count*32 <=
total_size` before the equality, so a forged count cannot wrap.

## 9. Determinism

**[producer]** The same input must produce the same bytes: rows sorted by fingerprint, lists
sorted by id, pool in row order and then list order, reserved fields zeroed, percentages rounded
half away from zero. Two runs over an unchanged library produce byte-identical files, which is
what makes a regenerated Snapshot safe to compare and safe to skip.

## 10. Version 2: Playlists

A **Playlist** is an ordered selection of the Library's Tracks. It is stored here because this
image is the one artifact the host produces for the device (ADR 0051); it is not a second model
of what the Library contains.

Limits: `list_count` 0..64, `list_rows` 0..8192 across the image, a list's name under 64 bytes,
and a list's own `row_count` may not exceed `list_rows`. A producer that breaks a limit refuses to
write rather than truncating.

### List record (32 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `list_id` | FNV-1a 64 of the list's name and creation time, strictly ascending |
| 8 | 4 | `name_offset` | offset into the pool of the list's NUL-terminated name |
| 12 | 4 | `row_start` | index of the list's first row in the list-row array |
| 16 | 4 | `row_count` | number of rows in this list |
| 20 | 8 | `created` | Unix seconds (UTC) when the list was made |
| 28 | 4 | `reserved` | zero |

`list_id` is opaque to the device: it is what `select` and the NVS key carry, so renaming a list
must not change it. Two lists in one image may not share an id — the producer detects the
collision and refuses, exactly as it does for rows.

### List row (12 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `fingerprint` | FNV-1a 64 of the Track's relative path (§6) |
| 8 | 4 | `path_offset` | offset into the pool of the Track's relative path |

A row is both a **binding** and a **resolution**: the path is what the device looks the Track up by
(`library_find`), and the fingerprint is what lets it refuse a row whose file is no longer the one
that was put in the list — the device recomputes `FNV-1a(path, size)` from the Library node and
compares. A mismatch, or a path the Library does not hold, means the row is **skipped** at
playback and reported as missing; it is never deleted and never silently played as something else.

Like a measurement row, a list row binds a size without storing one: the size is in the path's
fingerprint, and the Library node supplies the size to check it against.

## 11. Size

| Tracks | Rows | Pool (est.) | Version 1 | + 10 lists x 200 rows |
|---:|---:|---:|---:|---:|
| 1,000 | 32 KB | ~110 KB | ~142 KB | ~180 KB |
| 5,000 | 160 KB | ~550 KB | ~710 KB | ~750 KB |
| 20,000 | 640 KB | ~2.2 MB | ~2.8 MB | ~2.8 MB |

The pool dominates, and it is the part a Snapshot can do without: at 20,000 tracks the
partition would need to grow or the strings would need to be de-duplicated. 5,000 — the stated
target — fits a 2 MB partition with room for a second axis of growth. Lists are small next to it:
2,000 rows are 24 KB of records, and their names and paths are shared with nothing but themselves.

## 12. Versioning

Bump `format_version` for any change to this document, including adding a field to the
`reserved` bytes. A version 2 reader **accepts versions 1 and 2**: version 1 is a valid image with
no lists, and a producer with nothing to say about lists keeps writing it, so an older device can
still apply what a newer page sends. A reader refuses any version it does not know; a Snapshot is
cheap to regenerate, so there is no reason to guess at a half-understood image.
