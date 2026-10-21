# The analyser runs entirely in the browser

The webapp has to measure files a user selects on their own machine. A Node or Python
service would be faster per track, can scan folders, and would not need to ship 24 MB of
weights to every visitor — but it also means a backend to run, an upload path for the user's
music, and two runtimes to maintain for a tool whose audience is one person dropping a few
files. The browser path costs a few seconds per four-minute track on an M2 Max, which is fast
enough for this use.

## Decision

All decode, analysis, caching and export happen in the tab. A single Web Worker owns
Essentia.js (WASM) and ONNX Runtime Web; ORT's WASM loader is copied into `public/ort/` and
referenced by absolute URL, and the models are served from the app's own origin because
`essentia.upf.edu` sends no CORS headers. There is no server and no
upload.

## Considered options

- **Node/TS server with a browser UI.** Better for 10 000-track libraries and folder scanning,
  but adds operational surface and moves the user's files off their machine.
- **Browser first with a filesystem seam.** The analyzer takes decoded buffers and an ONNX
  Runtime interface, so a Node worker can reuse `src/audio/analyzer.ts` later without
  rewriting the measurement code.
- **Python sidecar.** Native Essentia is 2.3–4× faster than WASM, but it is a second runtime
  for a gain that only matters at library scale.

## Consequences

- The model set must be fetched before first use (`npm run models`); it is gitignored and
  remains under CC BY-NC.
- COOP/COEP headers are set on the dev and preview servers so ORT can use WASM threads;
  without them it runs single-threaded.
- Whole-library use is limited to repeated multi-file drops; a folder adapter needs the Node
  worker described above.
- `SOURCE_URL` must be set before a public deployment so the AGPL source offer is reachable.
