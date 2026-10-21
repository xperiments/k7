# Track analysis

A browser app that measures tempo, key, mood and energy for a user's own audio files. This
context covers the measurement of a file the user selects; it does not store, serve or play
music. The ESP32 that owns the card is a different context — see
[`CONTEXT-MAP.md`](../CONTEXT-MAP.md).

## Language

### The material

**Track**:
One MP3 or FLAC file submitted for analysis.
_Avoid_: song, item, audio file

**Folder**:
A directory the user drops or picks. Scanned recursively for tracks.
_Avoid_: directory, library

**Relative path**:
A track's location inside the folder it was added from, including the file name. Equals the
file name for a single-file drop. The content hash, not the path, identifies the track.
_Avoid_: full path, filepath

**Embedded metadata**:
Tags stored in the file itself — ID3 for MP3, Vorbis comments for FLAC.
_Avoid_: file info, tags (alone)

**Tagged value**:
A field taken from embedded metadata instead of computed. Marked as tagged and never silently
replaced by the analyser.
_Avoid_: cached value, existing field

**Tagged mood**:
A free-text mood label taken from embedded metadata, used instead of the six model
probabilities.
_Avoid_: mood tag (use for the model output)

**Content hash**:
The SHA-256 of a track's original bytes. Identifies the file independently of its name or
path.
_Avoid_: checksum, file id

**Model set**:
The exact ONNX models used for one analysis, identified as a set by their hashes.
_Avoid_: models, weights

**Analyzer version**:
The version of the analysis pipeline. Combined with the content hash and the model set, it
decides whether a cached analysis is still valid.
_Avoid_: app version

### The measurements

**Analysis**:
The complete set of measurements for one track.
_Avoid_: result, metadata, features

**Raw tempo**:
The detector's BPM before octave folding.

**Folded tempo**:
The raw tempo moved into 70–180 BPM by doubling or halving — the tempo presented to the
user.
_Avoid_: corrected tempo, bpm (reserve "BPM" for the number itself)

**Tempo confidence**:
A 0–1 heuristic from beat-interval consistency, used to decide whether deep analysis is
needed. Not a calibrated probability.
_Avoid_: accuracy, score

**Deep analysis**:
A re-analysis with the accurate tempo detector, triggered automatically when tempo
confidence is low, or requested for one track.
_Avoid_: retry, re-analysis

**Key**:
A tonic and a mode, for example A minor.

**Camelot code**:
The harmonic-mixing notation for a key, for example 8A. Derived from the key, not an
independent measurement.
_Avoid_: open key, wheel code

**Key strength**:
The key detector's correlation strength. Not a probability.
_Avoid_: confidence

**Mood tag**:
One of six binary classifier outputs — happy, sad, aggressive, relaxed, party, danceable. A
probability, not a label.
_Avoid_: mood, emotion

**Valence**:
Pleasantness, 0–1, from the mood model.

**Arousal**:
Intensity, 0–1, from the mood model.

**Energy**:
The name the interface gives to arousal — or the value of an energy tag when one is present.
It is not an independent model output when tagged.
_Avoid_: intensity (unless explaining the term)

### The work

**Queue**:
The tracks waiting to be analysed, in submission order.
_Avoid_: batch, library

**Tagged copy**:
A new file produced by writing an analysis into a copy of the original; the original is left
untouched.
_Avoid_: saved file, overwrite

**Stored analysis**:
The lossless analysis this app writes into a file's `XTAPE` tag and reads back instead of
measuring again.
_Avoid_: cache, saved result

**Cached analysis**:
An analysis reused because its content hash, analyzer version and model set all match.
_Avoid_: saved result
