# The Snapshot: measured music in PSRAM

**Goal.** A host measures a Library — tempo, key, energy, danceability, valence, arousal, mood,
names, genre — and writes the result to the card as one file. The device flashes that file into
its `snapshot` partition at boot, copies the image into PSRAM before the panel's first clock,
deletes the update file, and answers `library_search`, `library_track` and `library_stats` from
the copy. No query touches the card or flash; the image is resident, and the guard that hid its
flash reads is gone.

The decision and its evidence are [ADR 0047](../adr/0047-the-snapshot-lives-in-flash-and-is-never-resident.md),
amended by [ADR 0051](../adr/0051-a-playlist-lives-in-the-snapshot.md) and superseded in part by
[ADR 0069](../adr/0069-the-snapshot-is-resident-and-the-card-carries-the-selection.md).
The byte contract is [`snapshot-format.md`](./snapshot-format.md). Vocabulary is
[`CONTEXT.md`](./CONTEXT.md): **Snapshot**, **Update file**, **Track fingerprint**,
**Coverage**.

**Status of claims.** Every number here is arithmetic, a measurement or a target; where a number
is measured the text says so. §10 lists what the first board run has to measure.

---

## 1. Scope

### In scope

| # | Requirement |
|---|---|
| R1 | The device flashes `k7.db` from the card into a `snapshot` partition at boot, verifies it, deletes it, and copies the image into PSRAM before the panel's first clock |
| R2 | The device never reboots because of a bad Update file, and writes nothing to the card except the Update file's delete and the Playlist selection record (§2 U7) |
| R3 | The Snapshot is read from its PSRAM copy; no query reads the card or flash, and a normal boot reads the image from flash once, copies it and validates the copy |
| R4 | `library_search` filters by BPM, key, energy, danceability, valence, arousal, mood, genre, name, and sorts and pages |
| R5 | `library_track` and `library_search` return the measurements beside the track, `null` where a track has none |
| R6 | `library_stats` reports **Coverage** and the Snapshot's provenance |
| R7 | A track with no row is unanalysed, not hidden: it plays, lists, and matches by name |
| R8 | A version 3 image carries the host's **Playlists**, and the device can walk a selected one (ADR 0051; the selection is §2 U7) |

### Out of scope

Per-query card access; device-side analysis; a text index or any other built index; incremental
Snapshot updates (each image is complete); embeddings or vector similarity; a
`find_similar_tracks` tool; editing a Snapshot on the device — a new list arrives only as a new
image; MCP tools over Playlists (the page reads them at `/api/playlists`, and an agent that should
read them is a later decision); detecting a card swapped under a running device (unchanged from
today — the retry task only covers a card that was absent at boot).

---

## 2. The update flow

```text
host: scan the card's music directory, measure, write CARD/k7.db
card: back into the player
boot: census -> update (mount, flash, verify, delete) -> screen hardware -> services (copy to
      PSRAM, validate, scan the Library) -> the panel's first clock
```

**U1. The Update file is `k7.db` at the card's root** — `/sdcard/k7.db`, beside `music/`, never
inside it. One fixed name, one fixed place. The board still checks `/sdcard/xtape.db` second, for
a card written before the rename (ADR 0062), but the App writes `k7.db` only. Its length must not
exceed the partition.

**U2. The update runs before the panel.** The appliance mounts the card, and if `k7.db` exists
it runs the update, *then* brings the screen up. The order is forced: on this board the app's
code and rodata live in PSRAM (`SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA`), a flash write
disables the cache, and the panel's DMA cannot fetch from PSRAM while it is disabled
([`docs/research/panel-rgb-lvgl-performance.md:505`](../research/panel-rgb-lvgl-performance.md)).
The device is dark for the length of the write; nothing can be shown during it. The copy that
makes the image resident happens later in the same boot, still before the first clock (U9).

**U3. The write is bounded and checked.** `snapshot_apply()` takes a source and a target and
does the whole of it: validate the header, erase `ceil(total_size / 4096)` bytes, copy in 1 KB
chunks while checksumming, then read the partition back and compare. Only `SNAPSHOT_OK` lets
the caller delete the file. A failed write, a short read-back or a CRC mismatch leaves the file
in place and leaves the partition as it was. Both ends are injected, so the order is tested
with two buffers on the host (`t29_snapshot.c`), not only on a board.

**U4. A bad file never reboots.** Invalid header, unknown version, failed CRC, a directory in
place of a file, a file larger than the partition: log one line naming the reason, leave the
file alone, boot normally on the Snapshot already in flash (or none), and report the reason in
`library_stats`. The reboot is reserved for the one case in U5.

**U5. A card that arrives while the device is running reboots once.** The retry task mounts the
card; if `k7.db` is present it logs the fact and calls `esp_restart()`, without touching either
the card or flash. The next boot applies it under U2. There is no second reboot: after U3 the
boot continues into the normal sequence.

**U6. Interruption is recoverable by construction.** The card holds the master copy until the
delete. A power loss during erase or write leaves an image that fails the CRC, the reader
refuses it, and the next boot with the card present retries. A power loss after the delete
leaves a valid image and nothing to do. There is no second slot and no journal.

**U7. There are two card writes, and no more.** `remove()` of `/sdcard/k7.db`, after
U3's verification; and the Playlist selection in `/sdcard/k7.select`, a 32-byte CRC'd record at
the card root. `card_select_ensure()` creates the record as id 0 if it is absent, and
`card_select_save()` rewrites it when the choice changes, when it is cleared, or when the
broadcast drops the order. Its own task writes it, below the decoder, never an HTTP or broadcast
task. Nothing else is written: no rename, no directory, no repair, no cleanup of a stale file.
The premise in [`CONTEXT.md`](./CONTEXT.md) permits exactly these two.

**U8. The update mounts and unmounts its own card.** It runs before the services, so it brings
the card up, does its work and takes it down again; `app_services_start()` then mounts as it
always has. That is a second mount on the update path only, and the cost is a few hundred
milliseconds at boot in exchange for leaving the services' boot order untouched.

**U9. The image becomes resident before the first clock.** After the update, `app_services_start()`
maps the image, copies its `total_size` bytes into PSRAM, releases the mapping and validates the
copy (S2). The panel's first clock starts only after `app_services_start()` has returned, so the
copy never runs in front of a scanning panel. A failed allocation is no analysis, reported (S5),
not a fallback to the mapping.

---

## 3. The partition

```csv
# Name,   Type, SubType, Offset,   Size, Flags
snapshot, data, 0x40,    0x410000, 2M,
```

- `0x410000` is the first offset after the 4 MB app partition, and it is 64 KB-aligned, which
  `esp_partition_mmap` requires. The image is mapped at offset 0 with `total_size` bytes, copied
  into PSRAM and unmapped; nothing reads it in place.
- 2 MB is the ceiling for the file and for the image. At 5,000 tracks the image is about 260 KB
  (format §11), and the 3,485-track card measured 177 KB in v3 (306 KB as v2), so the partition
  is roughly eight times the 5,000-track target.
- `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, NULL)` is the only lookup. A build
  without the partition serves everything except the analysis, and says so.
- The partition holds a Snapshot image and nothing else: no filesystem, no wear levelling, no
  second copy, no header owned by the firmware. The image's own header is the whole of it. The
  selection record is not here; it is a file on the card, beside `music/`.

---

## 4. Reading

**S1. One module, two entry points.** `components/portable/snapshot/` exposes:

```c
/* Validate an image already in memory. The host tests pass a malloc'd file; the
 * device passes the PSRAM copy. `why` names the first refused rule. */
snapshot_result_t snapshot_open(const uint8_t *image, size_t size, snapshot_t **out);
snapshot_result_t snapshot_open_why(const uint8_t *image, size_t size, snapshot_t **out,
                                    char *why, size_t why_size);

const uint8_t *snapshot_image(const snapshot_t *s, size_t *size);
uint32_t       snapshot_generation(const snapshot_t *s);
uint32_t       snapshot_rows(const snapshot_t *s);
uint64_t       snapshot_path_hash(const char *relative_path); /* §6 of the format */
void           snapshot_close(snapshot_t *s);
bool           snapshot_find(const snapshot_t *s, uint64_t fingerprint, uint32_t size,
                             snapshot_row_t *row);
const char    *snapshot_row_string(const snapshot_t *s, const snapshot_row_t *row,
                                   snapshot_string_t which);

/* The predicate and the bounded collector the MCP layer drives. */
bool snapshot_matches(const snapshot_t *s, const snapshot_filter_t *f, const char *relpath,
                      uint32_t size, int64_t *sort_key, const char **sort_str);
snapshot_hits_t *snapshot_hits_new(uint32_t cap);
bool snapshot_hits_add(snapshot_hits_t *h, uint32_t index, int64_t key, const char *str);
uint32_t snapshot_hits_index(const snapshot_hits_t *h, uint32_t i);

/* Lists. A row resolves through the Library, never through the image. */
uint32_t  snapshot_lists(const snapshot_t *s);
bool      snapshot_list_at(const snapshot_t *s, uint32_t i, snapshot_list_t *list);
bool      snapshot_list_find(const snapshot_t *s, uint64_t id, snapshot_list_t *list);
uint32_t  snapshot_list_rows(const snapshot_t *s, const snapshot_list_t *list);
const char *snapshot_list_row_path(const snapshot_t *s, const snapshot_list_t *list,
                                   uint32_t row, uint64_t *fingerprint);

/* The update: source and target injected, so it is host-testable. */
snapshot_result_t snapshot_apply(const snapshot_source_t *src, const snapshot_target_t *tgt,
                                 char *why, size_t why_size);
```

The module copies nothing: `snapshot_find` reads the row out of the PSRAM image, and
`snapshot_row_string` returns a pointer into the pool.

`snapshot_open` is the seam that makes the format testable without a board: everything below it
works on a pointer and a length.

**S2. Validation happens once, at boot**, before the panel's first clock (so before MCP can
serve). It is the format's §9 list, in order, with the first failure returned as the reason. The
image is copied once into PSRAM (U9) — 177 KB on the 3,485-track card, ~260 KB at 5,000 — and the
CRC is computed over that copy; neither the image nor the CRC is read again.

**S3. Nothing is read from flash.** Rows and strings are read from the PSRAM copy. A query that
walks 5,000 rows touches 160 KB of PSRAM; the strings of a matched row are read to build the
response and are never cached.

**S4. Missing is a state, not a zero.** `snapshot_result_t` distinguishes: `NO_PARTITION`,
`NO_UPDATE`, `INVALID` (with a reason string), `NO_MEM` (the PSRAM copy could not be allocated)
and `OK`. The MCP layer turns them into Coverage and a reason, and every query still runs
against the Library.

**S5. There is no guard and no fallback.** ADR 0055's page guard existed because a mapped-flash
read displaces the scan-out; a read from PSRAM does not. So `page_service`,
`screen_display_page_service()` and its black-frame/re-establish path are gone, and nothing in a
query can black the screen. If the copy cannot be allocated the device answers without
measurements: `SNAPSHOT_NO_MEM` is logged with the byte count, `library_stats` reports `invalid`
with that reason, and the mapping is released. Keeping the mapping as a consolation would bring
back the read the guard was hiding, which is the fallback this decision removes.

---

## 5. The query engine

**Q1. Matching is over the Library, not the Snapshot.** The engine iterates the current
Library generation's track array, in broadcast order. For each track it builds the relative path
with `library_relpath()`, computes the **Track fingerprint**, and binary-searches the rows.
Nothing iterates the Snapshot.

**Q2. A row describes a track only if the fingerprint matches *and* the size matches.** A size
mismatch is an unanalysed track. This is how a replaced file stops wearing its predecessor's
BPM.

**Q3. Filters.** All optional; every one is an AND; a filter on a field a track does not have
excludes that track.

| Filter | Type | Matches |
|---|---|---|
| `query` | string, 1..255 B | case-insensitive ASCII substring of the relative path **or** of title, artist, album or genre; a track with no row can only match by path |
| `bpm_min`, `bpm_max` | number | `bpm_x10 / 10` within `[min, max]`, inclusive |
| `key` | array of strings | exact Camelot code (`8A`) or note name (`Am`, `A minor`, `F#m`), normalised to §5.2's code |
| `energy_min`, `energy_max` | integer 0..100 | the percentage |
| `danceability_min`, `danceability_max` | integer 0..100 | the percentage |
| `valence_min`, `valence_max` | integer 0..100 | the percentage |
| `arousal_min`, `arousal_max` | integer 0..100 | the percentage |
| `mood` | array of `happy`, `sad`, `aggressive`, `relaxed`, `party` | any listed bit set |
| `genre` | string | case-insensitive ASCII substring of the genre string |

**Q4. Sorting and paging.** `sort` is one of `path` (default, broadcast order), `title`, `bpm`,
`energy`, `danceability` — the Library keeps no duration, so there is nothing to sort by. Tracks with no value for the sort key sort last; ties
break by track index, so the order is total and deterministic. `offset` is 0..199 and
`limit` 1..100 (default 50), with `offset + limit <= 200`; the engine selects the top
`offset + limit` with a bounded array (≤ 200 entries) and then skips `offset`. No full sort, no
candidate array proportional to the library.

**Q5. Results are tracks, not rows.** Each result is the existing track object plus an
`analysis` object (Q6), so a caller never needs a second lookup.

**Q6. The `analysis` object:**

```json
{"bpm":124.5,"key":"8A","energy":78,"danceability":61,"valence":40,"arousal":78,
 "mood":["happy","party"],"title":"…","artist":"…","album":"…","genre":"…",
 "tagged":["bpm","key"],"confidence":72}
```

`energy`, `danceability`, `valence`, `arousal` and `confidence` are integers 0..100; `bpm` is
the folded value to one decimal; `key` is the Camelot code; `mood` lists the set bits; `tagged`
names the fields that came from tags (`bpm`, `key`, `mood`, `energy`). A field whose value is
unknown is `null` rather than absent — "not measured" is an answer the model can act on, and a
missing key cannot be told from a typo. `title`, `artist`, `album` and `genre` appear only when
the image has them. A track with no row has `"analysis": null`.

**Q7. Cost.** 5,000 iterations of FNV over ~60 bytes plus a binary search of ~12 steps is a few
milliseconds of core 1, well under one audio frame's slack, and it touches no card and no
allocation beyond the response.

**Q8. The engine is independent of MCP.** `snapshot_query()` takes a parsed filter struct and
returns indices into the Library; the JSON is MCP's business.

---

## 6. MCP surface

**M1. `library_search` keeps its name and grows.** Its description becomes "find tracks by
name and/or musical properties". `query` is no longer required; no parameter is. Its result is
the same envelope with `analysis` added per track, plus `matched` — how many tracks the filters
matched before `offset`/`limit` — and `truncated`, which reports whether anything was left
beyond the page. `matched` is what makes paging legible: without it `truncated` is the only
signal and the model cannot tell whether a second page exists.

**M2. `library_track` gains the same `analysis` object**, by path or, when control is enabled,
by the id/generation pair `player_play` already accepts.

**M3. `library_stats` gains:**

```json
"snapshot":{"state":"ok","format_version":3,"generation":1767225600,"rows":4913,
            "coverage":{"tracks":5000,"analysed":4913,"percent":98},
            "analyser":300,"model_set":"12345678901234567890",
            "tagged":{"bpm":812,"key":40,"mood":90,"energy":15}}
```

`state` is `ok`, `no_partition`, `no_update` or `invalid`, with `reason` when it is `invalid`.
A `NO_MEM` copy failure is the `invalid` state with its allocation reason. `coverage` counts the
*current* Library, not the image: rows that no longer match a track are not coverage.

**M4. The AI acts through the control tools.** A result's `path` is already what
`player_play` takes, so "play something like this" is search then play, two calls, no new tool.
Without `XTAPE_MCP_CONTROL` the read tools are unchanged and no play tool is advertised.

**M5. Budget.** `limit` ≤ 100 exists because the MCP layer builds the whole result in a 128 KB
PSRAM buffer and then escapes it into a second allocation of up to twice the size. At ~400 B of
JSON per analysed track, 100 rows is ~40 KB before escaping. A 200-row limit would put ~200 KB
of transient PSRAM under a query — a third of what the cover path leaves free
([`README.md:740`](../../README.md)) — for a page size nothing needs.

---

## 7. Failure modes

| Condition | Behaviour |
|---|---|
| No snapshot partition in the build | `library_stats` reports `no_partition`; search works on paths only |
| Partition present, never flashed | `no_update`; the card is the only way to get one |
| `k7.db` invalid, or a v1/v2/legacy image | Logged, left in place, boot continues; `invalid` with the reason; re-export the card as v3 |
| The PSRAM copy cannot be allocated | No analysis; `invalid` with the byte count; the mapping is released and the screen is not blacked |
| Power loss during erase/write | Partition fails the CRC; boot continues without analysis; the next boot with the card retries |
| Power loss after the delete | Image valid; nothing to do |
| Card absent at boot | No update; the retry task reboots once when a card with the file appears |
| Card absent and partition valid | Analysis answers normally; tracks are missing from the Library, so Coverage counts 0 |
| A row for a track that no longer exists | Ignored — matching is Library → Snapshot, never the reverse |
| A track with no row | Unanalysed: listed, playable, matches by name |
| The card changes without a new Update file | The old Snapshot stays; `coverage` drops as tracks stop matching |
| A list row's path is not in the Library | Skipped at playback, reported as missing; never deleted |
| A list row's path is re-encoded or replaced | Still resolves: a list row binds the path, not the file's size (format §8) |
| A selected list, then an image without it | Falls back to the broadcast order; `k7.select` is rewritten to id 0 |
| `k7.select` cannot be written (card out or read-only) | The choice lasts the session; one warning, not one per attempt |
| An image with a list area that fails the format's §9 rule 10 | The whole image is refused, as with any other invalid byte |

---

## 8. Budgets

| Resource | Cost |
|---|---|
| PSRAM at rest | the image: 177 KB on the 3,485-track card, ~260 KB at 5,000. The JSON view it replaces was 629,596 B / 615 KiB |
| PSRAM per query | result text (≤ ~40 KB at `limit` 100) + MCP's escaped copy (≤ ~80 KB) + top-K array (≤ 1.6 KB) |
| Card, boot without an update | the update's mount and one `stat` of `/sdcard/k7.db`, the services' mount, and one read of `k7.select` (created as id 0 if absent) |
| Card, boot with an update | the file read once (~0.18 s at 1 MB/s for 177 KB; ~0.26 s at 5,000 tracks) |
| Flash, boot with an update | erase + write + read-back of the image (177–260 KB) — **to be measured**, target ≤ 3 s |
| Flash, boot without an update | one header read and one copy of the image into PSRAM (177–260 KB) |
| Card, any query | zero |
| Flash, any query | zero |

---

## 9. Testing

**Host tier.** `snapshot_open` works on a buffer, so the format and the query engine are fully
testable without a board. T1–T8 live in `host/tests/t29_snapshot.c`, and the MCP shape of T7 in
`host/tests/snapshot_tests.py`, which builds an image with an independent writer and asks a
running host server the questions the feature exists for:

| # | Test |
|---|---|
| T1 | Images of 0, 2 and 5,000 rows open, and every sampled row is found by fingerprint and size — 5,000 is where the binary search and the size arithmetic have to hold |
| T2 | Every validation rule in the format's §9 has a corruption case: bad magic, a v2 or legacy image, wrong version, size mismatch, bad CRC, non-ascending rows, offset past the pool, unterminated strings, out-of-range field, non-zero reserved, `row_pool_size` larger than the pool |
| T3 | A forged `row_count` near `UINT32_MAX` is refused without overflow |
| T4 | Filter semantics: each filter alone, several together, unknown values excluded, `query` matching a title the path does not contain, genre as a substring |
| T5 | Sort and page: ties, tracks with no sort value last, `offset + limit` bounds, `truncated` correct |
| T6 | `analysis` is `null` for a track with no row, and `tagged` names exactly the tagged fields |
| T7 | The MCP JSON shape for `library_search`, `library_track` and `library_stats` matches the documented examples |
| T8 | Determinism: the generator run twice over the same input produces identical bytes |

The selection record has its own host tier, `host/tests/t31_card_select.c`: the round trip, the
id-0 record and a refusal for every damaged form.

**Board tier.** These are the ones the host cannot establish:

| # | Test |
|---|---|
| T9 | Flash window: an update boots, verifies, deletes, and continues; log the erase/write time |
| T10 | Invalid file: the device boots normally, reports `invalid`, and does **not** reboot in a loop |
| T11 | Hot insert: boot without a card, insert one with a file, observe exactly one reboot and then `ok` |
| T12 | Power loss during the write (pull power mid-flash) leaves `invalid`, and the next boot with the card retries to `ok` |
| T13 | Query latency with the stream running: `library_search` at `limit` 100 against a 5,000-track Snapshot, with starvation rebases and encoder share before and after |
| T14 | The card is untouched after the update except for the missing `k7.db` and the `k7.select` record |
| T15 | PSRAM copy failure: an allocation that cannot be met leaves `invalid` with the reason, no reboot and no black frame |

---

## 10. Open measurements

1. **The flash window's real duration** at 5,000 tracks — the only number that decides whether
   the boot update is acceptable or needs a progress indication.
2. **The boot copy and the query cost from PSRAM** — the copy's duration at 177–260 KB, and a
   query measured cold and warm at a few milliseconds.
3. **The response at `limit` 100** — the JSON size, the escaped copy, and whether the transient
   PSRAM is visible against the 504 KB the cover path leaves.
4. **Name matching at 5,000 tracks while streaming** — the scan is the only per-query CPU cost,
   and it runs on the encoder's core.
5. **Whether resolving the Music directory survives macOS** — the fingerprint is over bytes, and
   a normalising filesystem is the first thing that could disagree with the card.
