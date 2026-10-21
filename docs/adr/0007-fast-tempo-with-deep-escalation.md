# Fast tempo by default, accurate tempo on demand

Essentia offers two useful tempo trackers with a large speed gap. Measured in the WASM build
on a four-minute track: `BeatTrackerDegara` 1.1 s, `RhythmExtractor2013(multifeature)` 7.5 s.
The accurate one is also the only one with a documented confidence calibration — Essentia
says its (1.5, 3.5] confidence band is "accuracy around 80% in AMLt", while Degara's
confidence is always zero. Running multifeature on every file would make the queue feel
broken; running Degara alone would hide exactly the tracks where tempo is hard.

## Decision

Every analysis runs Degara first and derives a 0–1 confidence from the fraction of inter-beat
intervals within ±5 % of the median. If that confidence is below 0.6, or fewer than eight
beats were found, the worker immediately re-runs the same audio through multifeature and
replaces the tempo, recording `engine: "deep"` and `escalated: true`. A `Deep analyse` button
runs multifeature manually for any track. Folded BPM (70–180) is the presented value; the raw
value is kept alongside it.

## Considered options

- **Multifeature only.** One path and a calibrated confidence, but every track costs 7.5 s
  and the UI has no fast result to show.
- **Degara only.** Fastest, but no calibrated confidence and no way to recover a bad estimate.
- **A neural beat tracker (Beat This, MIT).** Best documented beat accuracy, but its ONNX
  route is community work, the model is tens of megabytes, and it would replace a dependency
  the project already ships for key measurement.

## Consequences

- A result carries the engine that produced it, and the cache key includes the analyzer
  version, so behaviour changes invalidate old results.
- The confidence is a heuristic, not a probability; the README and the UI both say so.
- Worst case is a low-confidence track costing Degara plus multifeature, about 8.6 s in the
  browser, and only for tracks that need it.
- The escalation threshold is a constant in `src/domain/tempo.ts` with unit tests; tuning it
  does not require touching the worker.
