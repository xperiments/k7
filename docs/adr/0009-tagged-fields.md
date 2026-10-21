# Tagged fields are trusted; the rest is measured

DJ libraries already carry tempo and key in their tags. Recomputing them wastes the most
expensive part of the analysis, and it can overwrite a value a human checked in Rekordbox,
Serato or Mixed In Key. Mood and energy tags are rarer and less standardised, but the same
argument applies: if the file states a value, measuring it again is both slower and
surprising.

## Decision

On a cache miss the app reads embedded metadata with `music-metadata` (ID3 for MP3, Vorbis
comments for FLAC) before decoding, and each field decides its own work:

- a positive `TBPM`/`BPM` number becomes the tempo, folded into 70–180 BPM, with
  `source: "metadata"`, no confidence and no tempo engine;
- a `TKEY`/`key`/`initialKey` value is parsed as Camelot (`8A`) or a note name (`Am`,
  `A minor`, `F#m`, `Bb`). A parse it cannot trust is ignored and the key is measured;
- a `TMOO`/`MOOD` string becomes a tagged **mood label**, and the six EffNet tag heads are
  skipped;
- an `ENERGY`/`TXXX:ENERGY` value (0–1, 0–100 or `%`) becomes tagged **energy**, and the DEAM
  model is skipped. Valence and arousal are only produced by DEAM, so they are absent in this
  case;
- if mood is tagged but energy is not, DEAM still runs for energy and the V/A axes; if energy
  is tagged but mood is not, EffNet still runs for the six tags and the V/A axes stay absent;
- if both BPM and key are tagged, the 44.1 kHz decode is skipped; if mood and energy are also
  tagged, no model runs at all and the result is pure metadata plus duration.

Tagged fields are marked in the result (`source`), shown as `tagged` in the UI, and are never
silently replaced.

## Considered options

- **Measure everything and use the tag only as a comparison.** Complete and consistent, but
  slower and it cannot preserve the tagged value as the displayed one.
- **Ignore tags entirely.** Simple, but wrong for the DJ-library use case this app targets.
- **Map a tagged mood onto the six probabilities.** Invents scores the file never contained;
  rejected. A tagged mood stays a label.
- **Run both mood models whenever either field is tagged.** Simpler code, but it re-measures a
  field the user asked us to trust, and it is exactly the cost the feature exists to avoid.

## Consequences

- The cache key includes the content hash, so editing a tag invalidates the cached analysis
  and the next run sees the new tag. The analyzer version is bumped (0.3.0) so analyses cached
  before mood/energy tagging are not reused.
- A wrong tag is displayed as tagged until the user changes it. `Measure tempo` re-runs the
  accurate tracker and flips the tempo source to `measured`; the other tagged fields are only
  re-measured by changing the file's tags.
- `confidence`, `strength`, `valence`, `arousal`, `energy` and the mood window are nullable;
  null means "not measured", not zero.
- The app depends on `music-metadata` (MIT), loaded on demand so it does not weigh on the
  first paint.
- Energy values outside 0–100, non-numeric strings such as `high`, and unparseable keys are
  ignored and measured instead.
