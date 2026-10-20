# The library is built once at boot and requests resolve through its tree

The server has no library object today: the broadcast re-walks the flat Music directory with
`readdir()` on every pass, and the index re-walks and `stat()`s it on every request. That cannot
grow into recursive playback, a JSON view or an MCP endpoint without making every consumer a
filesystem walker. The device is also constrained in a way a desktop is not: internal RAM is
scarce, the card is on a 20 MHz SPI bus, and a request must not pay for a directory walk.

The design is recorded in full in
[the research document](../research/server/s13-library-tree-and-mcp.md). This ADR records the
decision that the rest of the work depends on.

## Decision

The device builds **one immutable Library tree at boot** from the Music directory, recursively,
and keeps it in PSRAM as a single arena. The tree defines what exists, the order of the Playlist,
and where a Track lives.

- **The tree is the model.** Directories and Tracks are nodes; a directory owns a contiguous,
  name-sorted range of child edges; tracks are a flat array in depth-first order. Names live in
  one string table, full paths are built from parent links, and nothing points outside the
  arena. The Playlist is that array; its order is depth-first with name-sorted children,
  interleaving directories and Tracks.
- **Requests resolve through the tree.** HTTP and MCP turn a relative path into a node by
  walking the tree; a request string never becomes a filesystem path by concatenation. A
  download opens the path built from the matched nodes. The lookup removes path-joining
  attacks, but the route still rejects `.`/`..`, NUL and empty segments before lookup, and the
  scan skips symlinks; these are validation, not containment.
- **The arena is the wire format.** A versioned header, fixed-width little-endian records, the
  edge and track arrays, and the string table are served verbatim as the binary view. JSON is a
  second, prebuilt view of the same generation. Both have an exact length and a generation/ETag;
  neither touches the card per request.
- **Generations are immutable.** A rescan builds the next generation off to the side and
  publishes it atomically. The broadcast keeps its current generation until it finishes the
  Track, and a slow JSON reader keeps the generation it started with. The old arena and JSON
  are freed when the last reader releases them; only one scan runs at a time.
- **The path ceiling is `FILENAME_MAX`.** The VFS prepends `"0:"` into a `FILENAME_MAX + 3`
  buffer; in this build `FILENAME_MAX` is 1024, not the 255 fallback, verified by preprocessing
  the actual build. The scan enforces the absolute path against that value and logs a skipped
  path rather than letting the VFS truncate it. The HTTP URL cap (512 raw bytes) is lower and is
  a separate limit; the existing parser already answers an over-long raw target with 414.
- **The budget is bounded.** The tree and its JSON have a 4 MiB cap (and a 20 000-track cap).
  Reaching it truncates the scan, sets `truncated`, logs why, and keeps serving what was
  indexed. The arena is allocated from PSRAM; internal RAM is left to Wi-Fi and the encoder.
- **The scan is synchronous in v1.** It runs after the card mount and before the broadcast and
  HTTP listener. The board measurement decides whether that stays or becomes an explicit
  `indexing` state.

## Considered options

- **Keep the per-request walk (status quo).** The broadcast and the HTML Index each walk the
  card, there is no recursion, no JSON and no stable order, and the HTML Index costs O(files)
  `stat()`s per request. Rejected: it cannot support the requested features without moving the
  cost into every request.
- **Keep a flat, sorted list of paths in PSRAM.** It serves the playlist and lookup, and it is
  smaller than a tree, but it cannot answer "what is under this directory" for the JSON view,
  `library_list` or the HTML Index without a second structure. The tree's extra edge array is
  small next to the name bytes; rejecting it would buy a little memory and cost the browsing
  model.
- **Build a library cache file on the card.** It would survive reboots, but FAT gives no
  reliable directory-change stamp to invalidate it, it adds boot writes and wear, and the tree
  is rebuilt in seconds. Rejected for v1; a stale cache is a worse failure than a scan.
- **Bypass the VFS with FatFs directly.** FatFs does not copy the whole path into a fixed
  buffer, so it would lift the `FILENAME_MAX` ceiling and hand the scan each entry's size from
  `FILINFO`. But it also requires a second file-I/O path for the decoder and downloads, and
  this build's ceiling is already 1024 bytes. Rejected as the default; it remains the measured
  fallback if the scan is too slow.
- **Stream the JSON from the arena on every request.** It would avoid the prebuilt JSON's
  memory, but it reintroduces per-request work and makes `Content-Length` a counting pass. The
  prebuilt view is 2 MB at 10 000 tracks, inside the budget; streaming is the fallback if the
  cap ever binds.
- **Start the broadcast on a top-level list and grow it.** It would shorten time-to-first-audio,
  but the Playlist would change under the listener and the HTML Index would be wrong for a while.
  Rejected: a half-built library is a consistency bug, not a loading state.
- **Asynchronous indexing first.** It keeps the HTTP diagnostic page alive during a long scan,
  but it adds a readiness state and a second boot path. Deferred until the board measurement;
  v1 pays the boot delay and has no partial state.

## Consequences

- **Boot cost is the new cost.** A recursive scan reads one directory sector per entry; the
  research document models roughly 2.4–6.6 s for 5 000 tracks and 9.5–26 s for 20 000 using the
  repo's measured 250 KB/s card throughput. These are estimates, not board measurements; that is
  the first thing the board tier must measure.
  If a typical card exceeds ~2 s, the synchronous scan becomes the asynchronous `indexing`
  state described in the research document; the arena and the tree do not change.
- **Memory moves to PSRAM.** At 10 000 tracks the arena is about 0.83 MB and the JSON about
  1.96 MB, around 2.8 MB total against a measured ~7.98 MB free after the screen. The 4 MiB cap
  binds around 13 000–15 000 average tracks. The arena never falls back to internal RAM.
- **The library becomes a module with a narrow public surface.** The scan, lookup, JSON and
  binary views live behind one generation handle; the HTTP and MCP layers do not parse paths or
  walk directories. The broadcast consumes the track array instead of a `DIR *`, and the old
  directory-listing code is retired. The source-format rule is extracted first so the library
  and the broadcast can share it without a component cycle.
- **A rescan is visible only at a boundary.** The broadcast finishes its current Track on the
  old generation, the HTML Index and the JSON switch at the swap, and a reader that started
  before the swap finishes on the old bytes. The generation lease is what makes a 2 MB response
  and a rescan coexist without a global lock.
- **The path limit is a product limit, not a silent failure.** Absolute paths longer than
  `FILENAME_MAX` are skipped and logged; a URL longer than `HTTP_PATH_MAX` is rejected by the
  existing 414 rule, and a path between the two limits is playable by node id but not
  downloadable by URL. The README and the tests must state both when the feature is implemented.
- **The device stays diagnosable.** No card, no Music directory, an empty tree and a truncated
  tree are distinct states in the HTML Index and in `/api/library`; none of them is a 500 or a
  reboot. A card inserted after boot publishes a generation and starts the broadcast without a
  reboot.
- **The decision is hard to reverse.** Every consumer's API, the JSON schema, the binary
  layout, the test corpus and the boot order assume the tree. Returning to per-request walking
  would be a rewrite of the file surface, not a flag.

## References

- Design and evidence: [the research document](../research/server/s13-library-tree-and-mcp.md).
- Vocabulary: [`docs/specs/CONTEXT.md`](../specs/CONTEXT.md) — Music directory, Library tree,
  Playlist.
- Implementation tickets: `.scratch/library-tree/issues/01`–`14`.
