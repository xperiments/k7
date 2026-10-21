# A tagged copy carries the analysis, including an XTAPE manifest

Measured values should not disappear when the page closes. DJ tools expect BPM and key in the
file's tags, and a user who has just corrected a library should not have to analyse it again.
But a browser cannot overwrite a file the user dropped: there is no path and no handle. The
app can either write a copy for download, or ask for File System Access permission through a
picker — which only Chromium offers, and only for files the picker itself granted.

## Decision

Each result offers **Save tagged copy**. It writes standard tags for other players and a
lossless `XTAPE` JSON manifest for this app:

- MP3: `TBPM`, `TKEY`, `TMOO`, `TXXX:ENERGY`, `TXXX:XTAPE`;
- FLAC: `BPM`, `INITIALKEY`, `MOOD`, `ENERGY`, `XTAPE`.

The result is downloaded with the original filename; the user replaces the original. Nothing
is written in place. The reader prefers a valid `XTAPE` manifest over the standard tags, so a
saved copy restores the six mood probabilities and the valence/arousal axes rather than
collapsing mood to a single label. Invalid manifests are ignored and the standard tags or the
models take over.

Writing uses `taglib-wasm` in the main thread, loaded on demand. Model sessions are lazy as
well, so a fully tagged copy loads no ONNX weights at all.

## Considered options

- **Standard tags only.** Interoperable, but mood is one label and the probabilities and V/A
  are lost on re-read. Rejected as a regression.
- **In-place writing through `showDirectoryPicker`.** Better UX, but Chromium-only, requires
  the folder to be chosen through that picker (drag-and-drop grants no write handle), and
  introduces destructive writes into the app's first version.
- **A server that writes files.** Outside the browser-only decision in ADR-0006.

## Consequences

- Saving is non-destructive. The copy has the same name, so the user can replace the original
  in one move; there is no in-place option in v0.4.
- `taglib-wasm` (MIT wrapper, TagLib LGPL-2.1/MPL-1.1) adds ~1.4 MB of WASM, loaded only on
  the first save.
- The saved file's bytes change, so its content hash changes and it becomes a new cache entry.
  The `XTAPE` manifest is what makes the next analysis instant (93 ms in the browser smoke
  test, versus a full analysis).
- Standard tags are derived from the analysis: folded BPM, compact key, dominant mood label
  (or the supplied label), and energy rounded to two decimals. Decimal BPM is preserved in
  `TBPM`/`BPM` and read back with `parseFloat`.
- A stale `XTAPE` manifest wins over manually edited standard tags. Re-running the analysis
  and saving again refreshes it.
- Other software sees BPM, key, mood and energy; only this app sees the probabilities. That is
  the price of a lossless round trip without inventing a standard.
