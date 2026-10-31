# xtape track analysis

A browser-only webapp that measures **tempo, key, mood and energy** for MP3 and FLAC files.
Files never leave the machine: decode, analysis, caching and export all happen in the tab.

![The analyser showing tempo, key, energy and mood bars for a test track](docs/screenshot.png)

This is the implementation of
[`docs/research/bpm-key-mood-energy-webapp.md`](../docs/research/bpm-key-mood-energy-webapp.md).

## What it reports

- **Tempo** — folded into 70–180 BPM, with the detector's raw value and a confidence score.
  Fast analysis uses Degara and escalates automatically to the accurate multifeature tracker
  when confidence is low. If the file already has a `TBPM`/`BPM` tag, that value is used,
  marked **tagged**, and tempo analysis is skipped.
- **Key** — tonic and mode, the Camelot code, and the detector's correlation strength. A
  `TKEY`/`key`/`initialKey` tag that parses as Camelot (`8A`) or a note name (`Am`,
  `F#m`) is used and marked **tagged**; anything it cannot parse is measured instead.
- **Mood** — six probabilities from Essentia's EffNet heads: happy, sad, aggressive,
  relaxed, party, danceable.
- **Valence / arousal** — 0–1 regressions from the DEAM model. **Energy is arousal**, not a
  separate measurement.
- **Mood and energy tags** — a `TMOO`/`MOOD` tag becomes a tagged mood label (the six model
  probabilities are skipped); an `ENERGY`/`TXXX:ENERGY` tag (0–1, 0–100 or `%`) becomes tagged
  energy (DEAM is skipped). Valence/arousal are only present when DEAM ran, so a tagged energy
  with a measured mood shows the bars without the V/A plot.

![A fully tagged file, a mood-tagged file, and an energy-tagged file](docs/tagged.png)

Results are read-only and are cached in IndexedDB by content hash, analyzer version and model
set. `Download JSON` exports the analyses with provenance.

## Quick start

```sh
cd web
npm install
npm run models     # ~24 MB of ONNX weights, checksum-pinned, gitignored
npm run dev
```

Then open the printed URL, drop files or a folder, and wait for the cards.
`dev` also copies ONNX Runtime's WASM loader into `public/ort/`.

`npm run models` fetches from `essentia.upf.edu` and verifies every SHA-256 against
`weights.lock.json`; a mismatch fails the command rather than loading different weights.
> If `npm` on this machine cannot write `~/.npm` (a permissions issue seen during
> development), add `--cache .npm-cache` to the install command. That directory is ignored.

## Scripts

| Command | What it does |
|---|---|
| `npm run dev` | Vite dev server with cross-origin isolation headers |
| `npm run build` | Typechecks and builds the local app into `dist/` |
| `npm run build:shell` | Builds `dist-shell/`, the device shell with no weights or model files |
| `npm run preview` | Serves the production build with the same headers |
| `npm test` | Vitest: domain logic, mel parity, real-model pipeline |
| `npm run models` | Fetches and verifies the ONNX model set |
| `npm run assets` | Copies the local runtime files to `public/ort/` and `public/weights/` |
| `npm run icons` | Regenerates the 192 and 512 PNG icons |

## How it works

1. A dropped folder is walked with `webkitGetAsEntry` (or the folder picker's
   `webkitRelativePath`) and every MP3/FLAC found is queued with its relative path.
2. The main thread hashes the file and checks the IndexedDB cache.
3. On a miss it reads embedded tags with `music-metadata`. Each tagged field skips its own
   model: BPM, key, mood (EffNet heads) and energy (DEAM). If both BPM and key are tagged the
   44.1 kHz decode is skipped; if mood and energy are tagged too, no model runs at all.
4. Decoding uses `OfflineAudioContext` — mono 44.1 kHz for tempo/key, mono 16 kHz for the
   models. Duration comes from the decoded samples, so MP3 encoder padding is handled.
5. A single worker runs Essentia.js (WASM) for Degara beats and `KeyExtractor` (when they are
   not tagged), then, if confidence is below 0.6, Essentia's multifeature tracker.
6. Mood uses a 60 s window starting a quarter of the way in. The log-mel front-end is
   implemented in TypeScript (`src/audio/mel.ts`) and verified against Essentia's WASM
   implementation in `tests/mel.test.ts` (max band difference ≈3e-5). EffNet supplies the six
   tag heads when mood is not tagged; MusiCNN supplies the DEAM valence/arousal head when
   energy is not tagged.
7. Results are cached by `sha256(file) : analyzer version : model set` and exported as JSON.
   Tagged fields carry `source: "metadata"` and a null confidence/strength; measured fields
   carry `source: "measured"`.
8. Saving tags loads `taglib-wasm` on demand, writes the standard fields plus the `XTAPE`
   manifest into a copy of the bytes, and downloads it. A saved copy's `XTAPE` tag takes
   precedence over its standard tags on the next analysis.

The worker loads ONNX Runtime's WASM loader from `public/ort/` by absolute URL, so Vite
never treats it as a source import (public files cannot be imported from source in dev); the
models come from `public/models/`. The dev and preview servers set COOP/COEP so ORT can use
multiple threads; without those headers it falls back to one thread.

### The device shell and the weights

`npm run build:shell` produces `dist-shell/`, the files packed into the device's 1 MB
partition. It contains our code only: `index.html`, the app bundle, the worker, the ORT
loader glue, the TagLib adapter, `sw.js`, the manifest, the icons and `weights.lock.json`.
It contains no models and no `.wasm` binary — that build is under 1 MB.

Everything else is a **weight**, loaded through `src/weights/`:

1. `weights.lock.json` (imported into the app) lists every file, its URL source and its
   sha256. The models are fetched from the repository at the pinned tag; Essentia, ONNX
   Runtime and TagLib come from jsDelivr at their published versions.
   The weights are licence-bound and are not committed in every checkout, so a release tag
   has to be created from a tree that includes `web/public/models/`; otherwise the first
   analysis is refused by the hash check, which is the correct failure.
2. A weight is fetched, hashed and only then handed to its runtime. A mismatch is refused
   and named. Essentia's JavaScript is imported from a blob URL after the check.
3. Verified bytes are cached in IndexedDB by URL and hash, and the service worker keeps a
   second cache under a name carrying the app version and the manifest hash.

`npm run dev` and `npm run build` use the same code with `WEIGHTS_MODE=local`: the weights
come from `public/models/`, `public/ort/` and `public/weights/`, so development needs no
network. A local build that has not run `npm run models` says so instead of analysing.

## Accuracy caveats

These are measurements, not ground truth for your library:

- Tagged BPM, key, mood and energy are trusted as-is; they can be wrong. The UI marks each
  tagged field. `Measure tempo` re-runs the accurate tracker, replacing a tagged BPM for that
  result; other tagged fields are only re-measured by changing the file's tags.
- Tempo confidence is a heuristic (fraction of beat intervals within ±5 % of the median), not
  a calibrated probability. Half/double tempo is normal; the folded value and raw value are
  both reported.
- Key detection is the weakest field. MIREX-class systems reach roughly 60–70 % on EDM, and
  Essentia publishes no accuracy figure for `KeyExtractor`. Treat the strength as a hint.
- Mood models lose 10–30 points when transferred to a different collection (e.g. happy 0.86
  cross-validation → 0.71 on MTG-Jamendo). The bars are probabilities, not labels.
- The app was smoke-tested with synthetic audio for pipeline correctness, not musical
  accuracy. See the research note for the source measurements.

## Limitations in v0.1

- MP3 and FLAC only; no server, no editing, no beat grid. Folder scans are sequential, so a
  large library takes minutes.
- Saving produces a download, not an in-place update; and saving one card at a time is the
  only batch story for now.
- One 60 s mood window; tracks shorter than that use the whole track.
- Local use only. `SOURCE_URL` in `src/config.ts` must be set before deploying publicly, so
  the AGPL source offer is reachable.
- Browser support is Chromium/Firefox-first (FLAC in `decodeAudioData` varies in Safari).

## Licence

- The app code is **AGPL-3.0**; see [`LICENSE`](./LICENSE).
- The ONNX weights are **CC BY-NC 4.0**, by the Music Technology Group (UPF), and are not
  committed to this repository. They are downloaded separately and remain under their own
  licence; the combined application is therefore **non-commercial**. Commercial use needs a
  proprietary licence from MTG.
- Model provenance and citations are in [`NOTICE`](./NOTICE).

The domain glossary for this context is [`CONTEXT.md`](./CONTEXT.md).
