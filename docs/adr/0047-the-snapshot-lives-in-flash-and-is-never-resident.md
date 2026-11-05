# The measurements live in a flash partition, and are never resident

> **Amended: the reload never reads the Snapshot, and the reads that remain are guarded.** The
> rule ADR 0055 establishes — the scan-out cannot survive any flash access — applies to the
> Snapshot's mapped reads exactly as it did to the Shell's page reads. The first thing that broke
> was a page reload of the App at its public origin: `library_stats` walked every Library track
> against the Snapshot to compute coverage on **every call**, so a reload that only wanted the
> image's header (`generation`, `rows`) displaced the picture on ~10,000 cache-line fetches. That
> walk is now cached (`mcp_refresh_coverage()`, computed once at boot and once per rescan, when
> the Library and Snapshot are already fixed), so a reload makes no flash read at all. The reads
> that are genuinely per-request — `library_search`, `library_track`, and the `/api/playlists`
> routes — still read the mapping, and each calls `page_service` first: the black-and-re-establish
> guard ADR 0055 chose for page loads, so the slip happens behind a black frame and the scan is
> re-established after. The Snapshot is not copied to PSRAM (the numbers in "Why flash and not
> PSRAM" still hold, and the copy is what the owner rejected as too large).

A design for this device proposed a custom binary music database on the card: fixed 64-byte
track records, sorted BPM and year indexes, categorical bitsets, an inverted text index, opened
by section offset and read from the SD card at query time. Its stated reason was that the
database is too big to hold in RAM — "the firmware should use direct file offsets instead of
loading the complete database into RAM".

The owner's first worry was the one that matters: *"I am worried about sd access while
queries."* This board's measurements say the worry was right and the reason was wrong. The
database it describes is under 1 MB; the device has 5.3 MB of free PSRAM at boot and 504 KB
left once the cover path is live. And the card is a 20 MHz SPI bus delivering 1.0–1.2 MB/s,
on which a plain 16-bit/44.1 kHz FLAC already spends 12.6 s of every 60 s just feeding the
decoder. A design that turns every query into card traffic is paying the appliance's scarcest
resource for a file it could simply hold.

The design that came out of the conversation holds it in neither place.

## Decision

The byte contract is [`docs/specs/snapshot-format.md`](../specs/snapshot-format.md); the
subsystem, its failure modes and its tests are [`docs/specs/snapshot.md`](../specs/snapshot.md).

- **The Library stays the only model of what exists.** The boot scan, the tree, the Playlist,
  the JSON view and the binary view are unchanged. A **Snapshot** annotates tracks; it never
  lists them, and its absence changes nothing about what plays.
- **A Snapshot is one file, not a database.** A header (magic, format version, generation,
  row count, string-pool size, CRC32, analyser version, model set), fixed-width rows **sorted
  by fingerprint**, and one UTF-8 string pool. No directory, no sections, no per-query seeks.
- **A row is one track's measurements, keyed by its fingerprint**: the path hash and the file
  size, then BPM ×10 folded to 70–180, key, energy, danceability, valence, arousal, five mood
  bits, a confidence, flags saying which values were measured and which were tagged, and one
  offset into the pool for title, artist, album and normalised genre.
- **No indexes, in the file or in RAM.** 5,000 rows of ~32 bytes is 160 KB; walking all of
  them is a few milliseconds. The BPM sorted array, the bitsets and the inverted postings the
  proposal specified are all replaced by "read the row". Sorting by fingerprint is what makes
  the lookup a binary search with no map to build at boot.
- **The file arrives on the card and is consumed once.** The host writes `xtape.db` at the
  card's root, a raw image of the partition. At boot, *before the panel, Wi-Fi and the
  broadcast*, the device validates it, writes it to a 2 MB flash data partition, reads it back
  and verifies the CRC, then deletes the file and continues booting.
- **A bad file never becomes a reboot loop.** Invalid header, unsupported version, failed CRC:
  log it, leave the file alone, boot normally on the old Snapshot (or none), report it in
  `library_stats`. The reboot happens only for an update that is going to be applied.
- **Queries read the mapped partition and nothing else.** No RAM copy, no SD access, no
  filesystem. A normal boot reads the image once to validate it, then never again.
- **One search tool.** `library_search` keeps its name and gains optional filters — BPM range,
  key *list*, energy, danceability, mood, genre, sort, offset, limit up to 100 — and returns
  the measurements beside the name, `null` where a track has no row. There is no
  `find_similar_tracks`: the model composes that from the same filters. `library_stats` grows
  a **Coverage** block. Because results carry the track's relative path, what search returns is
  already what `player_play` takes, so the AI can act on what it finds.

## Why flash and not PSRAM

PSRAM is not the spare resource the design assumed. Measured: **5,295,632 B free** at boot with
the panel up ([README.md:496](../../README.md)), and **504,300 B** once the Case backdrop and a cover are live
([README.md:731](../../README.md)). A 1 MB resident database would be 19 % of the boot margin and 2× the worst
case the UI already leaves itself; the 150 KB it actually needs would still be a permanent
claim on a heap the menu and coverflow path also insist on. Flash is 16 MB with the app using
4 MB, it is memory-mapped read-only, and a query reads it exactly where it lies.

## Why the flash write happens at boot

On this board the app's code and rodata run from PSRAM ([`sdkconfig.defaults.sunton:40-41`](../../sdkconfig.defaults.sunton),
`SPIRAM_FETCH_INSTRUCTIONS` and `SPIRAM_RODATA`). A flash write disables the cache, and with it
external RAM: the panel "CANNOT function if the external memory cache is disabled, such as
during OTA or NVS writes" ([`docs/research/panel-rgb-lvgl-performance.md:505`](../research/panel-rgb-lvgl-performance.md)). There is no
version of this operation that is invisible while the device is drawing and streaming. So it
goes where nothing is looking — boot, before the panel and the network — and the cost is one
reboot when the card reappears while the device is already running, and none at power-on.

## Why the file is deleted, and why there is only one slot

The card has to say what to do next, and the owner chose the file's own existence as the
signal: present means update, absent means nothing to do. That means the delete is part of the
protocol, not housekeeping — and it is the device's only write to the card, taken deliberately
and recorded in the premise (`docs/specs/CONTEXT.md`).

Two consequences follow, and both simplify the proposal:

- **No NVS generation stamp.** The card is the state; the device keeps none.
- **No two-slot scheme.** §9.3's transactional rename assumed the database's only copy was on
  FAT. Here the card holds the master until the delete, so an interrupted flash leaves a bad
  CRC, the boot refuses it, and the next boot with the card present retries. The card *is* the
  backup, which is why the delete may only follow a verified read-back.

## Considered options

- **Stream from the card per query, as proposed.** The smallest format, and the one that
  answers the wrong question: every query becomes a tenant on a bus the decoder already
  strains, and the prefetch experiment ([`docs/research/audio-decode-cost.md:69-77`](../research/audio-decode-cost.md)) already
  showed that a second card reader does not overlap — it sums.
- **Load the Snapshot into PSRAM at boot.** Simple, and correct on a board with room; this one
  does not have it, for the numbers above. The amendment at the top keeps the mapped read but
  hides it behind the same black-and-re-establish guard as a page load, because the copy is too
  large.
- **The proposal's full database, resident or mapped.** Names, dictionaries and a text index
  duplicated beside the Library's own, plus a rule for tracks that exist in one and not the
  other. Rejected: two descriptions of one truth, and a second search path to keep in step
  with `library_search`, which already matches names case-insensitively over relative paths.
- **Stage the upload over HTTP, then flash it.** No card writes at all, but the staged bytes
  must survive a reboot and PSRAM does not, so they would have to go to flash inside the
  request — a frozen screen and a dropped connection in exchange for a modal upload endpoint
  and an open device accepting a megabyte from anyone on the LAN.
- **An NVS generation stamp instead of deleting.** Equivalent behaviour with persistent device
  state, and one more way for the card and the device to disagree.
- **The proposal's ten mood flags.** Six of the ten have no source: the analyser produces five
  mood heads plus danceability, and two DEAM axes. Mapping sad→melancholic and
  happy→uplifting would invent values and present them as measured, which ADR 0009 exists to
  prevent.

## Consequences

- `partitions.csv` gains a 2 MB data partition at `0x410000` (the first offset after the 4 MB
  app), 64 KB-aligned so it can be mapped. The image is one file the host writes verbatim.
- **The premise gains one card write**: the delete of a consumed Update file, and nothing else,
  ever.
- **Track analysis now has a consumer.** The browser app stops being a dead end whose only
  output is a tagged copy; it produces the file the device runs on. The delivery is a download
  the user drops on the card — a directory picker is a later convenience, not the guaranteed
  path, because the app's intake is drag-and-drop and `<input webkitdirectory>`, which grant
  read only (ADR 0010).
- **ADR 0012 is not reopened.** That ADR rejected caching the *tree* on the card, because FAT
  gives no reliable way to know when a directory changed. The Snapshot caches nothing the scan
  produces: the tree is still rebuilt every boot, and a stale Snapshot can make a query
  incomplete, never make the Library wrong.
- **Staleness is visible, not silent.** A track with no row is unanalysed: listed, playable,
  searchable by name, no BPM, key or mood. A row with no track is ignored. A track edited or
  replaced under the same name is caught by the size in its fingerprint only if the size
  changed; renames lose the analysis. Coverage is the number that says so.
- **The fingerprint is a shared format, like the byte order.** Both sides hash the exact bytes
  of the relative path the filesystem returns; a disagreement costs a missing row, never a
  wrong one.
- **One more format to version.** A Snapshot whose format version the firmware does not know is
  refused with a log line, not guessed at.
- **Never measured**: the boot flash window's real duration, the mapped-partition query cost,
  and whether 5,000 rows of name matching in the arena is fast enough to stay under the
  encoder's shadow. These are the first board measurements the implementation owes.
