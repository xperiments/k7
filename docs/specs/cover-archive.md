# The cover archive

`k7.covers` at the card's root is the **browser's cover cache, carried on the card**. The App
writes it when a scan settles, and reads it back when the card is opened again. It is not the
device's artwork: the panel still reads a Track's own picture from the audio file. It exists so
a browser that has lost (or never had) its IndexedDB can take the already-fitted pictures from
the card instead of decoding and encoding every cover again.

The browser store is content-addressed (`web/src/storage/media.ts`): `covers` holds one record
per distinct **source** picture, keyed by the SHA-256 of the source bytes, and a `k7-media` row
carries only `coverHash`. `k7.covers` is the same set, in one file. On the 3,485-track card that
is **283 records**; the measured SVT-AV1 proxy at CRF 32 put their payloads at **8,356,896 B**,
and the browser's libavif encoder at quality 55 is expected to land in the same range. The same
cache stored as raw pictures per row was 291,095,922 B, because one album's art was copied into
every track.

## File

```text
+------------------------------------------------------------------+
| header      32 B   magic, version, sizes, count, total, CRC       |
| records     count x 52 B, strictly ascending by source hash      |
| payloads    the encoded pictures, in record order                |
+------------------------------------------------------------------+
```

`total_size = 32 + count * 52 + sum(length)`. Little-endian. The header CRC covers
`[32, total_size)`; a record's CRC covers its payload alone, so a damaged cover can be named
without rejecting the archive.

### Header

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 8 | `magic` | `K7COVR`, NUL-padded to 8 bytes |
| 8 | 2 | `version` | `1` |
| 10 | 2 | `header_size` | `32` |
| 12 | 2 | `record_size` | `52` |
| 14 | 2 | `reserved` | zero |
| 16 | 4 | `count` | records, `0..10000` |
| 20 | 4 | `total_size` | the file's length |
| 24 | 4 | `crc32` | CRC-32/IEEE of `[32, total_size)` |
| 28 | 4 | `reserved` | zero |

### Record (52 bytes)

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 32 | `hash` | SHA-256 of the **source** picture bytes |
| 32 | 2 | `width` | pixels, after the 422 fit; `0` when unknown |
| 34 | 2 | `height` | pixels |
| 36 | 1 | `format` | `0` AVIF, `1` JPEG, `2` PNG, `3` WebP, `4` GIF, `5` BMP, `6` unknown |
| 37 | 3 | `reserved` | zero |
| 40 | 4 | `offset` | absolute file offset of the payload |
| 44 | 4 | `length` | payload bytes |
| 48 | 4 | `crc32` | CRC-32/IEEE of the payload |

The hash is of the source, not of the AVIF: a row's identity is the picture the file carried,
and a new encoder version can rewrite the payload without orphaning the row. Records are sorted
by hash; the builder refuses duplicates. The payloads are the 422px longest-side AVIF the worker
produced, or the source bytes with their own format when the AVIF codec was unavailable.

## Writer

The App writes the archive after `k7.db`, in the same `writeToCard('run')` path, through the
same read-write card handle (`web/src/ui/analysis.tsx`). The list-only write does not touch it.
The builder is `buildCoverArchive()` in `web/src/storage/coverArchive.ts`; it is deterministic:
the same set of rows produces the same bytes, which makes the file safe to compare and skip.

## Reader

`identifyCard()` reads `k7.covers` when a card is opened and writes every record into the
browser's `covers` store (`restoreCoversFromCard()`, `web/src/ui/cardWriter.ts`). A scan then
finds each source picture by hash in `ensureCover()` and skips its encoding. A missing, older or
damaged archive is ignored: it is a cache, not a requirement.

The device does not need the file to play or draw. An HTTP route that serves it for a browser
that cannot open the card directly (`GET /api/covers`) is the natural next step; it would read
the SD card and touch no flash, so it does not contend with the panel.

## Limits

- 10,000 covers; 32-bit offsets and lengths; a 4 GiB file ceiling in practice.
- The archive is a cache: deleting it costs a re-encode, never a Track.
- It is written by the App, not the device. It joins `k7.db` (written by the App) and `k7.select`
  (written by the device) as the card's third state file, and the device never writes it.
