# The Snapshot is resident, and the card carries the Playlist selection

ADR 0047 put the Snapshot in a flash partition because PSRAM was the scarce resource: **504,300 B**
left once the cover path was live, and a resident database was rejected as too large. That was
true of the format it had. Version 2 spent 306 KB on the 3,485-track card, and every query read it
from mapped flash, which the rule ADR 0055 establishes cannot be done while the panel scans:
`page_service` blacked both framebuffers and re-established the scan after every
`library_search`, `library_track` and Playlists request.

Two measurements change the answer. Version 3 stores the same measurements in **177 KB** on that
card (~260 KB at 5,000 tracks), and the Library's prebuilt JSON view — **629,596 B** for that
card's Library, **615 KiB** at 5,000 — is gone. The image is now smaller than the JSON view it
replaces, so holding it releases PSRAM rather than spending it.

## Decision

- **Format version 3 only.** Rows are 32 bytes with four 3-byte string offsets and one packed
  `measurements` word; strings are interned; `row_pool_size` marks where the rows' strings end.
  There is no v1, no v2 and no `XTAPESNP` magic, in either the producer or the reader. The
  3,485-track image goes from 306 KB to 177 KB; at 5,000 tracks it is ~260 KB.
- **The image is copied into PSRAM at boot, before the panel's first clock.**
  `app_services_start()` maps the partition, copies `total_size` bytes, calls
  `app_board_snapshot_release()` and validates the copy. The first clock starts after that call
  returns, so the copy never shares the glass with a scan.
- **Queries read PSRAM; flash is unmapped.** `snapshot_find()` and `snapshot_row_string()` read
  the copy. No query reads the card, and after the copy no query reads flash.
- **A failed copy is an absence, not a black frame.** If the allocation fails the device answers
  without measurements: it logs `not enough PSRAM for the N-byte image`, `library_stats` reports
  `invalid` with that reason, and the mapping is released. It never falls back to a mapped read,
  because that is the read the guard exists to hide.
- **The guard is gone.** `page_service`, `screen_display_page_service()`, `PAGE_HOLD_MS`, the
  black-frame/re-establish path and the MCP and services hooks that called them are deleted.
  There is no Snapshot flash read left to guard.
- **The Library JSON view is gone.** Only `/api/library.bin` remains; `library_json()`,
  `library_cfg_t.build_json` and the JSON builder are removed. That freed **629,596 B** for the
  3,485-track card's Library and **615 KiB** at 5,000.
- **The Playlist selection lives in `k7.select`.** A 32-byte CRC'd record at the card root, beside
  `k7.db`: magic `K7SEL`, version 1, the list id at 12..19, and id 0 for no selection. It is
  card-only and written asynchronously by its own task, below the decoder; NVS's `playlist` key
  is gone.
- **The card write premise gains this second write.** ADR 0047's delete of the consumed Update
  file is no longer the only card write: the selection record is the other one, and nothing else
  is written.
- **The card image is re-exported as v3, in upgrade order.** Flash the firmware first, then the
  App, then the card. A v2 image is refused; an old firmware refuses a v3 image.

## Why the format changed

A row still has four names and nine values, and still fits 32 bytes: the four offsets are 24-bit,
and the BPM code, key, five percentages, mood bits and source flags share one little-endian
`uint64`. Interning
puts one copy of each distinct title, artist, album or genre in the pool, shared by every row
that uses it, and often by more than one field of one row. `row_pool_size` separates the rows'
strings from the lists' strings, so `patchListArea` can replace the list area without walking
the row dictionary, and the reader can validate each region separately.

The 3,485-track card measured the difference: 306 KB as v2, 177 KB as v3. The byte contract is
[`docs/specs/snapshot-format.md`](../specs/snapshot-format.md); the producer is
[`web/src/domain/snapshot.ts`](../../web/src/domain/snapshot.ts) and the reader is
[`components/portable/snapshot/snapshot.c`](../../components/portable/snapshot/snapshot.c), which
refuses any magic but `K7SNAP` and any version but 3.

## Why resident now

ADR 0047 measured **5,295,632 B** free at boot with the panel up, and **504,300 B** once the
cover path is live. On the 3,485-track card the resident image is 177 KB, roughly a third of that
margin; at 5,000 tracks it is ~260 KB, roughly half. It is affordable because the JSON view it
replaces was larger still: 629,596 B for that card's Library, 615 KiB at 5,000. The copy is one
`memcpy` at boot on a bus with nothing scanning yet, and the read it removes is the one that
forced a black frame on every query.

The copy also ends the module's relationship with the partition. `app_board_snapshot_image()`
reads the header, maps the image, and `app_board_snapshot_release()` unmaps it as soon as the
copy exists, so a later `app_board_snapshot_image()` maps it again rather than returning a stale
pointer.

## Why the selection moved to the card

NVS is flash, and the selection was written from the broadcast task or an HTTP worker. ADR 0055's
rule makes that a flash access while the panel scans, which is why it went through the flash
writer. The card is already the Snapshot's transport and already holds the lists; a 32-byte
record is cheaper than a flash job, and an SD write does not take the PSRAM window.

[`components/portable/card_select/`](../../components/portable/card_select/) owns the record:
`K7SEL`, version 1, the id at 12..19, reserved zero, CRC-32/IEEE over the first 28 bytes, and id
0 for no selection. `card_select_ensure()` creates the id-0 record at boot if the file is absent
and leaves a present one alone; `card_select_save()` rewrites it. The appliance owns a
length-one queue and a writer task at priority 2, below the decoder's 3: a select, a clear, or
the broadcast dropping the order posts the newest id and returns, and the task does the write. A
card that is absent or read-only leaves the choice standing for the session, logs once, and
never blocks the caller. There is no NVS key and no fallback.

## Migration

The version is a break in both directions: a v3 reader refuses v2 and `XTAPESNP`, and an older
firmware refuses `K7SNAP` v3. The order is therefore forced — **firmware, then App, then the
card**. Flashing the App first would have it write an image the device refuses; writing the card
first would put a v3 image under older firmware. A v2 image on the card is refused and left in
place, and the device serves without analysis until a v3 image is applied.

## Consequences

- **PSRAM at rest is no longer zero.** It is the image — 177 KB on the 3,485-track card, ~260 KB
  at 5,000 — plus the reader's state. The JSON view that went away was larger, so the net change
  is still a release.
- **Boot gains one copy; queries lose every flash read.** The copy happens before the first
  clock, so it is not a visible window.
- **The black-frame guard is gone.** ADR 0055's rule still governs the update write and Wi-Fi's
  NVS at boot; it no longer has a Snapshot reader to guard.
- **`k7.select` is a new file at the card root.** A card used by a v3 device gets a 32-byte id-0
  record the first time it boots; a read-only card still runs, with the choice lasting the
  session.
- **The card's write premise is two writes**, the update delete and the selection record (ADR
  0047, amended here).
- **v2 cards must be re-exported.** The device will not read the old image.
- **The tests moved with it.** [`host/tests/t29_snapshot.c`](../../host/tests/t29_snapshot.c)
  pins v3 only, the v2 and legacy-magic refusals and the packed word;
  [`host/tests/t31_card_select.c`](../../host/tests/t31_card_select.c) pins the 32-byte record
  and every damaged form; `t30_playlist_select.c` pins the selection. The boot copy is
  board-tier pending.

## Files

The reader and format: `components/portable/snapshot/snapshot.c`,
`components/portable/snapshot/include/snapshot.h`. The resident copy:
`components/portable/app_services/app_services.c`, `components/app/board_esp32/board_esp32.c`.
The guard removal: `components/app/screen_display/screen_display.c`,
`components/portable/mcp/mcp.c`. The selection: `components/portable/card_select/card_select.c`,
`components/app/appliance/appliance.c`. The JSON removal:
`components/portable/library/library.c`, `components/portable/library_http/library_http.c`. The
producer: `web/src/domain/snapshot.ts`, `web/src/playlists/image.ts`.
