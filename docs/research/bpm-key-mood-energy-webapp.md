# BPM, key, mood and energy for a webapp that ingests MP3 and FLAC

**Status:** research only; no source changed. Written 2026-10-20 on the `xtape` checkout
(`abcb8f7` plus untracked working-tree files). All timings were taken on this machine: Apple
M2 Max, 12 cores (8P + 4E), 32 GB, macOS 26.3.1, Node 24.18.0, ffmpeg 9.0.2 (Homebrew),
Python 3.12.13 for the native reference measurements.

**Question:** a webapp must take MP3 and FLAC files, extract BPM, key, mood and energy for
each one, and do it fast without giving up accuracy. It may use ONNX models or ordinary
tools. What should it actually run, where should that run, and what will it cost in speed
and licence terms?

**Decision (2026-10-20):** the project is **open source and non-commercial**. That settles
the licence question: the AGPL `essentia.js` build plus the CC BY-NC MTG weights are usable,
so the note's default is the Essentia path — not the permissive Beat This/CLAP workaround in
§8.2. The build should be **browser-first** (§8.1); a Node worker is the same analysis code
behind a different file adapter, for whole-library runs.

**Verdict, in eight lines:**

1. **Build it as a TypeScript webapp and do the analysis in JS/WASM/ONNX, not Python.**
   Everything needed exists on the JS side: `essentia.js` (WASM) for tempo and key,
   `onnxruntime-web` in the browser or `onnxruntime-node` on the server for mood.
   Python remains useful only as a faster reference implementation for bulk server work.
2. **Tempo has a cheap and an accurate mode, and the library says so itself.** Essentia's
   `BeatTrackerDegara` is documented as "significantly higher" speed with "lower accuracy
   than BeatTrackerMultifeature", recommended for "batch processings of large amounts of
   audio signals"; `RhythmExtractor2013` (`multifeature`) is the accurate mode and the only
   one with a calibrated confidence. If the AGPL is a problem, Beat This (MIT code **and**
   weights) is the accuracy-first alternative via a community ONNX port.
3. **Key is cheap.** `KeyExtractor` costs 0.40 s per 4-minute track in WASM (0.10 s native)
   and returns a profile-correlation strength that can serve as confidence. Use it, but
   expect ~60–70 % on EDM-like material, not 95 %.
4. **Mood is a model, not a formula, and the model weights are the licence problem.**
   Essentia/MTG's heads are CC BY-NC (the two official pages disagree on ND vs SA) 4.0 —
   fine for a personal or open-source tool, not for a paid product without a proprietary
   licence. The code licence is separate (Essentia AGPL-3.0); CLAP (Apache-2.0 weights) is
   the permissive mood route.
5. **Use EffNet, not MusiCNN, for throughput.** Measured single-thread: EffNet embeddings
   ≈ 8.2 ms/patch, MusiCNN ≈ 46 ms/patch — 5.6× slower for a 3 MB model. MusiCNN/VGGish are
   only needed for the valence/arousal (DEAM) heads, which have no EffNet variant.
6. **"Energy" is a product definition, not a standard field.** Spotify's audio features are
   closed to new apps (2024-11-27) and AcousticBrainz is gone. Either expose the DEAM
   `arousal` axis as the learned version, or define energy from loudness, onset rate and
   spectral flux and say so.
7. **A 4-minute track costs roughly 2 s in the browser and 1–3 s server-side, per
   worker.** Decoding is negligible (0.08–0.31 s); the tempo tracker and the CNN dominate.
   A pool of 8 workers turns that into roughly 130–300 tracks/minute on this machine.
8. **Cache by content hash + analyzer/model version, and store confidence per field.**
   Half/double tempo and ambiguous keys are normal; a webapp that silently returns one
   number per field will be wrong often enough to lose trust.

Labels used below: **VERIFIED** = read in a primary source cited on the line;
**MEASURED** = produced on this machine, command in the appendix; **ESTIMATE** = arithmetic
from cited or measured numbers, assumption stated; **UNVERIFIED** = could not confirm.

---

## 1. What the four fields can honestly be

**BPM** is a tempo estimate, and tempo is only defined up to an octave: 87 and 174 BPM are
the same pulse at different metrical levels. Every detector therefore needs a folding policy
(usually 70–180 BPM) and a confidence number. Essentia's `RhythmExtractor2013` returns `bpm`,
a beat-interval array and a `confidence` scalar for `multifeature` — but the same page warns
that with `degara` "there is no confidence estimation … the output confidence value is
always 0" ([reference](https://essentia.upf.edu/reference/std_RhythmExtractor2013.html)), so
the fast path has to derive its own confidence from beat-interval consistency. Beatless
intros, rubato, DJ mixes and tempo changes are the failure modes; a single number hides them.

**Key** is a 12×2 classification (tonic × major/minor) with a strength. Correlation against
tonal profiles is the classical method; Essentia's `KeyExtractor` exposes 16 profile types
(`diatonic`, `krumhansl`, `temperley`, `weichai`, `tonictriad`, `temperley2005`, `thpcp`,
`shaath`, `gomez`, `noland`, `faraldo`, `pentatonic`, `edmm`, `edma`, `bgate`, `braw`) and a
`strength` output ([reference](https://essentia.upf.edu/reference/std_KeyExtractor.html)).
Relative major/minor is genuinely ambiguous in many recordings, so the UI should show the
Camelot/Open-Key code and let the user correct it.

**Mood** has no ground truth. The open models split into two families:

- discrete binary heads (`mood_happy`, `mood_sad`, `mood_aggressive`, `mood_relaxed`,
  `mood_party`, `mood_acoustic`, `mood_electronic`) plus `danceability` and `moods_mirex`
  (5 clusters);
- continuous valence/arousal regressors trained on DEAM, emoMusic and MuSe
  ([Essentia model catalogue](https://essentia.upf.edu/models.html)).

The reported accuracies are in-house cross-validation and, where available, transfer to
MTG-Jamendo — not a promise about your library (see §5).

**Energy** is not a field any of these models emits. Spotify's `energy` was a proprietary
0–1 perceptual estimate; the Audio Features endpoint has been unavailable to new Web API
use cases since 2024-11-27
([Spotify announcement](https://developer.spotify.com/blog/2024-11-27-changes-to-the-web-api)).
AcousticBrainz, which published Essentia-derived BPM/key/mood, shut down and its own
post-mortem named poor key/BPM quality and the absence of confidence values as reasons
([MetaBrainz](https://blog.metabrainz.org/2022/02/16/acousticbrainz-making-a-hard-decision-to-end-the-project/)).
Both are warnings: define energy explicitly and attach confidence.

---

## 2. Where the analysis can run

| | Browser only | Node/TS server | Python sidecar |
|---|---|---|---|
| **Tempo/key** | `essentia.js` WASM (AGPL-3.0) | same WASM, or a native sidecar | native Essentia (AGPL-3.0) |
| **Mood/energy** | `onnxruntime-web` + ONNX heads | `onnxruntime-node` + ONNX heads | TensorFlow/ONNX + heads |
| **Decode** | `AudioContext.decodeAudioData` (MP3 everywhere; FLAC in Chromium/Firefox) | ffmpeg child process, or native decoder | ffmpeg / libsndfile |
| **Library-scale input** | File System Access API, Chromium only ([caniuse](https://caniuse.com/native-filesystem-api)) | a folder on disk, any browser | a folder on disk |
| **Privacy** | files never leave the machine | files stay on your server | same |
| **Speed per 4-min track** | ≈2 s, single worker (≈30 tracks/min per tab) | ≈1–3 s per worker | ≈1 s per core (native tempo) |
| **Licence exposure** | ships AGPL code and NC weights to users, so it cannot be closed-source | AGPL only if you use Essentia; a SaaS does not distribute GPL | same |

The webapp can be either shape; the algorithms are identical. The browser shape is the
better product for "drop a few files and see the numbers" and for privacy. The server shape
is the only sensible one for "analyse my 10 000-track library", because the browser cannot
walk a filesystem portably and a tab must stay open.

**Why the Python reference still matters.** Native Essentia is 2.3–4× faster than the WASM
build for the same algorithm (measured below), so if the app ever grows a batch backend, a
Python worker behind the same JSON contract is the cheap upgrade. It is not needed for v1,
and making it the primary path would mean shipping two runtimes to the browser-less user.

### 2.1 Decoding MP3 is where accuracy leaks

FLAC is lossless and its `STREAMINFO` block gives the exact sample count, so there is nothing
to get wrong. MP3 is the opposite: a VBR file's duration is only correct if the Xing/Info
header (and the LAME tag's encoder delay/padding) is present and honoured. Measured here on a
180.000 s LAME VBR encode:

| File | `ffprobe` duration | Decoded mono samples |
|---|---|---|
| original, with Xing header | 180.000 s | 7,938,000 (exact) |
| remuxed with `-write_xing 0` | **164.33 s** | 7,939,584 (padding left in) |

That is an 8.7 % duration error and a 36 ms offset at the start, from the container alone —
enough to break a beat grid and to make a "last 30 seconds" analysis window wrong. Two rules:
never derive duration from bitrate, and do not trust a decoder's reported length for analysis
windows; count decoded samples instead. In the browser, `decodeAudioData` handles the trimming
per platform but resamples to the `AudioContext` rate, so create it at 44.1 kHz and derive
timing from the returned buffer, not from ID3/Xing metadata. For the UI, a tag library that
understands Xing (`music-metadata` in JS) can show a duration without decoding the file.

---

## 3. Tempo: what to run

| Option | Runtime | Accuracy evidence | Speed (4-min track) | Licence |
|---|---|---|---|---|
| `RhythmExtractor2013` (`multifeature`) | essentia.js WASM / Essentia native | `BeatTrackerMultiFeature` fuses five onset functions; its confidence band (1.5, 3.5] is documented as "accuracy around 80% in AMLt" ([docs](https://essentia.upf.edu/reference/std_BeatTrackerMultiFeature.html)) | **7.5 s** WASM / **2.2 s** native | AGPL-3.0 |
| `BeatTrackerDegara` | essentia.js WASM / native | docs: "lower accuracy than BeatTrackerMultifeature … significantly higher [speed] … reasonable … for batch processings"; **no confidence output** ([docs](https://essentia.upf.edu/reference/std_BeatTrackerDegara.html)) | **1.1 s** WASM / **0.47 s** native | AGPL-3.0 |
| `PercivalBpmEstimator` | essentia.js WASM / native | loop-oriented BPM, no beat grid ([docs](https://essentia.upf.edu/reference/std_PercivalBpmEstimator.html)) | **3.1 s** WASM / **0.72 s** native | AGPL-3.0 |
| TempoCNN (Schreiber & Müller) | TensorFlow/C++ only | Acc0/Acc1 74.2 %/92.1 % on the 7-dataset combined set; GiantSteps Acc0 73.0 % vs madmom 58.9 % ([paper](https://zenodo.org/record/1492353/files/141_Paper.pdf)) | not measured here | AGPL-3.0 ([repo](https://github.com/hendriks73/tempo-cnn)) |
| madmom | Python only | Acc0/Acc1 69.5 %/93.6 % on the same set ([paper](https://zenodo.org/record/1492353/files/141_Paper.pdf)) | not measured here | BSD-2 code, **CC BY-NC-SA weights** |
| Beat This! (ISMIR 2024) | PyTorch; ONNX via community ports | GTZAN beat F1 89.1, downbeat 78.3; Ballroom 97.5 vs RWC Classical 77.1 ([repo](https://github.com/CPJKU/beat_this), [paper](https://arxiv.org/abs/2407.21658)) | no published latency | **MIT code and weights** |
| librosa `beat_track` | Python only | no published accuracy; octave prior centred on 120 BPM ([source](https://github.com/librosa/librosa/blob/main/librosa/feature/rhythm.py#L452-L461)) | **0.25 s** native | ISC |

**Recommendation:** default to `BeatTrackerDegara` when the app can carry AGPL, derive BPM
from its beat times (median inter-beat interval, then fold into 70–180), and use the
interval spread as a confidence signal. Offer "deep analysis" as
`RhythmExtractor2013(method: 'multifeature')` — that is the only mode with a documented
confidence calibration ("around 80% in AMLt" in the (1.5, 3.5] band) — for tracks where the
fast estimate is low-confidence or the user asks. Do not silently run multifeature on every
file in the browser: at 7.5 s/track it costs more than everything else combined.

**If the AGPL rules out Essentia, use Beat This ONNX.** Its code *and published weights* are
MIT, which no other accurate beat tracker here matches. The model is ~20 M parameters, takes
30 s of 22.05 kHz audio as a 128-bin mel spectrogram, and is normally run from PyTorch; the
ONNX route is community work (MIT conversions at ~80–186 MB, plus a C++ ORT port at
[beat_this_cpp](https://github.com/mosynthkey/beat_this_cpp), opset 14, 1500-frame chunks).
That is a real integration risk and there is no published CPU latency, so benchmark it before
committing. In the browser without AGPL, `web-audio-beat-detector` (MIT) is the fallback, but
its own README only claims "surprisingly good results especially for electronic music".
aubio (GPL-3.0) and SoundTouch (`BPMDetect`, LGPL-2.1) are the other C/C++ options; both are
older detector designs and neither has a browser story.

The accuracy numbers also quantify the octave problem: strict accuracy (Acc0) around 70 %
becomes 92–94 % once octave errors are forgiven (Acc1). Most tempo "errors" are the right
pulse at the wrong metrical level, which is exactly what a fold toggle and a confidence
number are for.

**Input constraint:** `BeatTrackerDegara` requires 44.1 kHz input to work correctly (same
docs page), so decode to mono 44.1 kHz for the DSP path and resample to 16 kHz separately
for the CNN path. That is two cheap decodes or one decode plus one resample.

**Fold, don't trust.** A 120 BPM detector output on a drum & bass track is usually 174/2.
Store `bpm_raw` alongside `bpm` and the fold rule, so a future version can change the policy
without re-decoding audio.

---

## 4. Key: what to run

`KeyExtractor` in the WASM build supports the same profiles as native, returns
`{ key, scale, strength }`, and costs **0.40 s** for a 4-minute file in the browser (0.10 s
native). It is the obvious choice: one call, a confidence value, no model.

The alternatives are worse for a webapp:

- **libKeyFinder / KeyFinder** (GPL-3.0, used by Mixxx, maintained by the Mixxx team
  [here](https://github.com/mixxxdj/libKeyFinder)) is C++ only. Using it server-side is
  possible but it is GPL, and it means a second native dependency for a field that already
  costs 0.4 s in WASM.
- **librosa + Krumhansl templates** (ISC) is Python, and its template matching is a small
  part of Essentia's profile machinery; porting it to TS is easy but there is no public
  benchmark showing it is better than `KeyExtractor`.
- **Manual/embedded tags** (`initial_key`, `TKEY`) should be read first and used when
  present; DJ libraries often already carry a human-checked key.

**Accuracy is the weak field.** MIREX 2019's best systems scored 84.19 % correct on the
MIREX2005 key set but 69.04 % on GiantSteps (EDM) ([results](https://www.music-ir.org/mirex/wiki/2019:Audio_Key_Detection_Results));
a 2016 MIREX profile comparison put Temperley profiles at 63.42 %/34.11 % and the
edma/bgate lineage at 60.70 %/62.09 % on the same two sets ([abstract](https://www.music-ir.org/mirex/abstracts/2016/FJH3.pdf)).
Essentia publishes no figure for `KeyExtractor`, and with `bgate`/`edma` it sits in that
profile-matching family, so expect roughly 60–70 % on EDM-like material, not 95 %. The
MIREX wiki was intermittently unreachable while this note was written; the numbers above are
as reported in the abstracts, and the lesson does not depend on the exact digit.

If key accuracy matters more than dependency count, KeyFinder is the accuracy name in this
space: Mixxx reports it "performs considerably better" than its default detector at about
64× the cost ([Mixxx](https://mixxx.org/news/2021-04-08-new-in-2-3-keyfinder/)), and it is
GPL-3.0 C++ — server-side only.

**Recommendation:** use `KeyExtractor` (`bgate` default, `edma` as an alternative), store
`strength`, map to Camelot, and plan the 100-track labelled check (§9) rather than asserting
a number. Read an existing `initial_key`/`TKEY` tag first if present; it is often
human-checked.

---

## 5. Mood and energy: models, metrics, and what they cost

### 5.1 The heads

Essentia's model catalogue lists the classifiers, their input embeddings and their reported
metrics. The important column is the second accuracy figure: the same models, evaluated on a
manually annotated ~1000-track subset of MTG-Jamendo with a different collection and
annotators, lose 10–30 points. That transfer number is much closer to what a personal
library will see than the cross-validation number.

| Head | Classes | 5-fold CV | MTG-Jamendo transfer | Model card |
|---|---|---|---|---|
| `mood_aggressive` | aggressive / not | 0.98 | **0.74** | [json](https://essentia.upf.edu/models/classification-heads/mood_aggressive/mood_aggressive-msd-musicnn-1.json) |
| `mood_acoustic` | acoustic / not | 0.94 | **0.82** | [json](https://essentia.upf.edu/models/classification-heads/mood_acoustic/mood_acoustic-msd-musicnn-1.json) |
| `danceability` | danceable / not | 0.94 | **0.72** | [json](https://essentia.upf.edu/models/classification-heads/danceability/danceability-msd-musicnn-1.json) |
| `mood_electronic` | electronic / not | 0.93 | **0.83** | [json](https://essentia.upf.edu/models/classification-heads/mood_electronic/mood_electronic-msd-musicnn-1.json) |
| `mood_party` | party / not | 0.91 | **0.76** | [json](https://essentia.upf.edu/models/classification-heads/mood_party/mood_party-msd-musicnn-1.json) |
| `mood_relaxed` | relaxed / not | 0.89 | **0.73** | [json](https://essentia.upf.edu/models/classification-heads/mood_relaxed/mood_relaxed-msd-musicnn-1.json) |
| `mood_sad` | sad / not | 0.89 | **0.72** | [json](https://essentia.upf.edu/models/classification-heads/mood_sad/mood_sad-msd-musicnn-1.json) |
| `mood_happy` | happy / not | 0.86 | **0.71** | [json](https://essentia.upf.edu/models/classification-heads/mood_happy/mood_happy-msd-musicnn-1.json) |
| `tonal_atonal` | tonal / atonal | 0.97 | **0.66** | [json](https://essentia.upf.edu/models/classification-heads/tonal_atonal/tonal_atonal-msd-musicnn-1.json) |
| `moods_mirex` | 5 clusters | 0.67 | — | [json](https://essentia.upf.edu/models/classification-heads/moods_mirex/moods_mirex-msd-musicnn-1.json) |
| `deam` (valence/arousal) | regression | valence CCC 0.778, arousal CCC 0.647; RMSE 0.635 / 0.674 | — | [json](https://essentia.upf.edu/models/classification-heads/deam/deam-msd-musicnn-2.json) |

Source for both accuracy columns: Essentia's TensorFlow paper, Table 4
([arXiv:2003.07393](https://arxiv.org/abs/2003.07393)); the individual model cards report
slightly different CV values (e.g. `mood_aggressive` 0.97) but the same picture. The 5-class
MIREX model at 0.67 is the honest illustration of how hard mood is: the binary heads look
great because the classes are broad.

### 5.2 ONNX availability

The model page advertises ONNX versions but links only one `.onnx` file; the rest are
available by substituting the extension. I verified by HTTP 200 **and** by running them:
`msd-musicnn-1.onnx` (3.17 MB), `mood_happy-msd-musicnn-1.onnx` (82 KB),
`deam-msd-musicnn-2.onnx` (82 KB), `danceability-msd-musicnn-1.onnx` (82 KB),
`discogs-effnet-bsdynamic-1.onnx` (18.0 MB), plus EffNet heads (`mood_happy-discogs-effnet-1.onnx`,
`danceability-discogs-effnet-1.onnx`, `mood_aggressive-discogs-effnet-1.onnx`, ~514 KB each).
Signatures, read from ONNX Runtime:

- MusiCNN embedding model: input `melspectrogram [batch, 187, 96]` (16 kHz, 96 mel bands),
  outputs `embeddings [batch, 200]` and 50 MSD tag activations.
- EffNet embedding model: input `melspectrogram [batch, 128, 96]`, outputs
  `embeddings [batch, 1280]` and 400 Discogs tag activations.
- Classifier heads: input `[batch, 200]` (MusiCNN) or `[batch, 1280]` (EffNet), output
  `[batch, 2]` (or 5 for MIREX), softmax applied by the graph.

**Licence does not change with the format.** The weights remain CC BY-NC-ND 4.0 per the
[licensing page](https://essentia.upf.edu/licensing_information.html) — which also says
CC BY-NC-SA 4.0 on the [models page](https://essentia.upf.edu/models.html), a
self-contradiction worth raising with MTG before relying on either wording — with a
proprietary licence on request.

### 5.3 The speed trap: MusiCNN vs EffNet

Measured, one thread, on the same 80-patch workload (a 4-minute track is ≈93 EffNet patches
or ≈64 MusiCNN patches, so compare the per-patch column rather than the totals):

| Model | Load | Inference, 80 patches | Per patch | Output |
|---|---|---|---|---|
| `discogs-effnet-bsdynamic-1.onnx` | 32 ms | **0.66 s** | 8.2 ms | 1280-d |
| `msd-musicnn-1.onnx` | 28 ms | **3.70 s** | 46.2 ms | 200-d |

That is the same 80-patch workload for both. The patch sizes differ — EffNet takes 128 mel
frames (2.56 s at 50 fps) and MusiCNN 187 frames (3.74 s) — so one 4-minute track is ≈93
EffNet patches (≈0.76 s) or ≈64 MusiCNN patches (≈2.9 s) at one thread. With four threads the
EffNet figure drops to ≈3.0 ms/patch (≈0.28 s for 93 patches, measured in Python). So EffNet
is the throughput choice by a wide margin, and its 1280-d embedding is the one most of the
newer heads use. MusiCNN is only required for the DEAM valence/arousal heads (no EffNet
variant exists — probing `deam-discogs-effnet-2.onnx` returns 404), and a 30-second excerpt
(≈8 patches) brings that down to ≈0.37 s per track.

### 5.4 Energy

Two defensible definitions:

- **Learned:** use DEAM `arousal` (0–1) as the energy axis and `valence` as the mood axis.
  This is reproducible, has published correlation numbers, and gives you a 2-D mood map
  instead of a bag of labels. Cost: MusiCNN, so run it on an excerpt.
- **Computed:** define energy from measurable signal properties and document the formula —
  short-term loudness (EBU R128 / LUFS), onset rate (beats per second or onsets per second),
  and spectral flux. These are cheap (RMS 0.012 s, EBU R128 1.01 s for 4 minutes) and
  explainable, but not perceptual ground truth: a quietly mastered loud track can score low.

Do not ship an "energy" that is really Spotify's number re-derived; there is no public
mapping, and Spotify itself deprecated the endpoint. If the UI needs a single 0–100 slider,
compute it as a within-library percentile rank of the composite and label it
"energy (relative to this library)". Store the ingredients so the formula can change without
re-analysing.

**If the weights must be permissive:** CLAP (LAION, Apache-2.0 weights) produces a 512-d
audio embedding; a small ridge/logistic head trained on a few hundred of your own labels
turns it into a mood or energy scorer, and zero-shot prompts ("aggressive", "calm") work
without labels but are noticeably weaker. musicnn (ISC) and OpenL3 (MIT) are older
permissive embeddings; MuQ and MERT are stronger but their weights are CC-BY-NC.

A reasonable result schema is therefore `bpm{bpm, confidence, raw}`, `key{key, scale,
strength, camelot}`, `mood{tags: {happy: 0.81, ...}}`, `energy{lufs, onset_rate, flux,
composite, arousal?}` plus `provenance{decoder, analyzer_version, model_versions}`.

---

## 6. Measured performance

All numbers on the M2 Max described above, best of three where repeated, single-threaded
unless stated. Test material: a 240.000 s stereo 44.1 kHz WAV (sine + pink noise), encoded to
MP3 320 kbps and FLAC 16-bit. **Only the timings are meaningful**: the material is synthetic,
so the BPM/key/mood values it produces are noise and are not evidence about accuracy.

### 6.1 Single components, 240 s track

| Step | Runtime | Time | × realtime |
|---|---|---|---|
| ffmpeg decode MP3 → mono f32 44.1 kHz | native | 0.28 s | 857× |
| ffmpeg decode FLAC → mono f32 44.1 kHz | native | 0.08 s | 3000× |
| ffmpeg decode MP3 → mono 22.05 kHz | native | 0.28 s | 857× |
| `soundfile` read FLAC stereo | native | 0.24 s | 1000× |
| `soundfile` read MP3 stereo | native | 0.16 s | 1500× |
| librosa `beat_track` | native | 0.25 s | 960× |
| librosa `chroma_stft` | native | 0.25–0.29 s | 850× |
| librosa `chroma_cqt` | native | 0.56–1.19 s | 200–430× |
| Essentia `RhythmExtractor2013(multifeature)` | native | 2.24 s | 107× |
| Essentia `RhythmExtractor2013(degara)` | native | 0.47 s | 510× |
| Essentia `PercivalBpmEstimator` | native | 0.72 s | 333× |
| Essentia `KeyExtractor` (default `bgate`) | native | 0.11 s | 2180× |
| Essentia `KeyExtractor(edma)` | native | 0.10 s | 2400× |
| Essentia `LoudnessEBUR128` | native | 1.01 s | 238× |
| Essentia `RMS` | native | 0.012 s | 20000× |
| `essentia.js` `RhythmExtractor2013` | WASM (Node) | 7.56 s | 32× |
| `essentia.js` `BeatTrackerDegara` | WASM (Node) | 1.09 s | 220× |
| `essentia.js` `PercivalBpmEstimator` | WASM (Node) | 3.14 s | 76× |
| `essentia.js` `KeyExtractor` | WASM (Node) | 0.40 s | 600× |
| ONNX EffNet embeddings, 80-patch workload | ORT Node, 1 thread | 0.66 s | — |
| ONNX EffNet embeddings, one 4-min track (≈93 patches) | ORT Node, 1 thread | ≈0.76 s | — |
| ONNX MusiCNN embeddings, 80-patch workload | ORT Node, 1 thread | 3.70 s | — |
| ONNX MusiCNN embeddings, one 4-min track (≈64 patches) | ORT Node, 1 thread | ≈2.9 s | — |
| librosa mel spectrogram (96/128 bands, 16 kHz) | native | 0.03–0.04 s | 6000× |

Notes: the first librosa resample call costs ~7 s of one-off numba/soxr initialisation;
subsequent calls are 0.02 s. `essentia.js` `arrayToVector` costs 0.044 s for 10.6 M samples.
ONNX Runtime session creation was 28–32 ms for the small models and 32 ms for EffNet
(weights already on disk).

### 6.2 Whole-track budgets

| Pipeline | Components | Per track | Throughput (8 workers) |
|---|---|---|---|
| Fast, WASM/browser | decode 0.3 + Degara 1.1 + key 0.4 + EffNet on a 60 s excerpt 0.19 | **≈2.0 s** | ≈240 tracks/min in 8 tabs/workers; **one tab ≈30/min** |
| Fast, native/server | decode 0.3 + Degara 0.47 + key 0.11 + EffNet on 60 s 0.1 | **≈1.0 s** | ≈300 tracks/min (measured shape: 338/min for decode+Degara+key at 8 processes, before adding the CNN) |
| Accurate, native | decode 0.3 + multifeature 2.24 + key 0.11 + EffNet on 60 s 0.1 | **≈2.75 s** | ≈150 tracks/min |
| Accurate + valence/arousal | add MusiCNN on a 30 s excerpt 0.37 | **≈3.1 s** | ≈130 tracks/min |

The 8-worker figures are **ESTIMATE** (per-track × 8, ignoring I/O contention and the E-core
slowdown) except where a measurement is named. Two measured data points support the shape:
with 4 processes and 8 tracks, Essentia `degara` + key ran at 183 tracks/min; with 8
processes and 16 tracks, 338 tracks/min. Those were native Python processes, so browser
throughput will land lower.

**Excerpts are the real speed lever.** Tempo and key are cheap enough to run on the full
track, but the learned mood pass only needs 30–60 s. On a 60 s excerpt the measurements
scale almost linearly: `BeatTrackerDegara` 0.32 s, `KeyExtractor` 0.10 s, EffNet 23 patches
≈0.19 s, MusiCNN for arousal 8 patches ≈0.37 s. That makes a "mood + energy" pass cost
~0.6 s per track and leaves the full track for tempo/key.

### 6.3 Why not just run librosa in the browser?

librosa is Python + numba; there is no maintained browser build. Its `beat_track` is fast and
its chroma features are fine, but porting the pipeline to TypeScript means reimplementing
onset detection, DP beat tracking and tonal profiles, and then owning the accuracy of that
port. `essentia.js` is the compiled, tested version of the same ideas and already runs in the
browser. If the AGPL is unacceptable, porting a small DSP subset is the alternative, not
importing librosa.

---

## 7. Licences decide the architecture

| Component | Licence | What it means for this webapp |
|---|---|---|
| Essentia (native) | AGPL-3.0 ([repo](https://github.com/MTG/essentia)) | SaaS triggers the network clause: users must be offered the source. MTG also sells a proprietary licence ([licensing](https://essentia.upf.edu/licensing_information.html)). |
| essentia.js | AGPL-3.0 ([repo](https://github.com/MTG/essentia.js), npm 0.1.3) | Shipped to the browser, so the source obligation follows the bundle, not just the server. |
| Essentia/MTG model weights | CC BY-NC-ND 4.0 (licensing page) / CC BY-NC-SA 4.0 (models page) | **Non-commercial.** A paid product needs a proprietary licence from MTG. |
| librosa | ISC ([repo](https://github.com/librosa/librosa)) | Permissive; Python-only. |
| aubio | GPL-3.0 ([repo](https://github.com/aubio/aubio)) | Fine server-side in a SaaS; not shippable in a closed browser bundle. |
| libKeyFinder | GPL-3.0 ([repo](https://github.com/mixxxdj/libKeyFinder)) | Same; C++ only. |
| madmom | BSD-2 code; model/data files CC BY-NC-SA ([repo](https://github.com/CPJKU/madmom)) | The weights are the trap, again. |
| TempoCNN | AGPL-3.0 ([repo](https://github.com/hendriks73/tempo-cnn)) | Same network clause as Essentia, with no web build. |
| Beat This! | MIT, **code and published weights** ([repo](https://github.com/CPJKU/beat_this)) | The cleanest accurate tempo path; PyTorch runtime, ONNX by community port. |
| BeatNet | CC BY 4.0 ([repo](https://github.com/mjhydri/BeatNet)) | Commercial use allowed with attribution; no official ONNX. |
| CLAP (LAION) | CC0-1.0 repo / Apache-2.0 weights ([HF](https://huggingface.co/laion/clap-htsat-unfused)) | The permissive embedding route for mood if you train your own head. |
| MuQ / MERT | MIT code / **CC-BY-NC 4.0 weights** ([MuQ](https://github.com/tencent-ailab/MuQ), [MERT](https://huggingface.co/m-a-p/MERT-v1-95M)) | Non-commercial weights again. |
| musicnn / OpenL3 | ISC / MIT | Commercial-friendly embeddings, older and weaker than EffNet. |
| SoundTouch | LGPL-2.1 ([site](https://www.surina.net/soundtouch/)) | Dynamic linking only; BPM-only detector. |
| ONNX Runtime (node/web) | MIT ([docs](https://onnxruntime.ai/docs/)) | Clean; the npm package is large (~291 MB installed for `onnxruntime-node`, mostly platform binaries). |
| ffmpeg (Homebrew build) | GPL-3.0-or-later ([legal](https://ffmpeg.org/legal.html), `--enable-gpl --enable-version3` in `ffmpeg -version`) | Fine to invoke as a subprocess locally; do not redistribute the binary in a closed product. An LGPL build is the redistributable option. |
| libsndfile | LGPL-2.1 | FLAC and (since 1.1.0) MP3 decoding. |
| Symphonia (Rust) | MPL-2.0 | Clean permissive decoder if you ever need a native/Node path without ffmpeg. |

Two clarifications that matter in practice:

- **GPL ≠ AGPL for a SaaS.** A hosted Node service that links a GPL library is not
  distributing it, so GPL-3.0 tools are usable server-side in a paid product. AGPL's network
  clause is different: the service's users must be able to get the source. If the app is
  open source, both are moot.
- **The model weights are not covered by the code licence.** You can be perfectly compliant
  with AGPL and still have no right to use the mood heads commercially. This is the
  constraint to resolve first if the webapp is a product.

---

## 8. Recommended stack

### 8.1 The shape

A single TypeScript codebase is enough.

**Default build: browser-only.** A static site (Vite is enough) with the analysis in a Web
Worker, deployed as plain files. There is no backend, no upload, and no database to operate.
The Node worker pool described below is the same analysis modules behind a filesystem
adapter, switched on only when someone wants to point the app at a folder.

- **Frontend:** React + Vite. Drag-and-drop, a batch queue, one result card per track with
  per-field confidence and an "edit" affordance.
- **Analysis:** a Web Worker (browser build) or a Node worker pool (folder build) running:
  1. decode to mono 44.1 kHz (Web Audio in the browser, ffmpeg server-side);
  2. `essentia.js` `BeatTrackerDegara` → beat times → BPM + confidence;
  3. `essentia.js` `KeyExtractor` → key/scale/strength → Camelot;
  4. resample to 16 kHz, mel (96 bands, 512/256) → EffNet ONNX → heads → mood tags;
  5. energy composite from loudness/onset rate/flux (and optionally DEAM arousal on a 30 s
     excerpt via MusiCNN).
- **Storage:** IndexedDB in the browser, SQLite/Postgres in the Node build, both keyed by
  `sha256(file) + analyzer_version`. Re-uploads hit the cache instantly; model upgrades are
  an explicit re-analysis, not a surprise.
- **No Python in v1.** If batch jobs later outgrow the JS tempo path, add a Python sidecar
  that consumes the same queue and writes the same JSON; the frontend does not change.

### 8.2 Which licence tier you are in

- **Open source, non-commercial (the chosen tier):** the AGPL and the non-commercial weights
  are both acceptable, so use them and stop looking for workarounds. Concretely:
  - licence the app **AGPL-3.0**, keep the repo public, and link to the source in the UI
    footer. For the static build that is the distribution obligation; if the Node build is
    ever exposed to users over a network, the same link covers AGPL §13;
  - **do not vendor the model weights.** Download them from `essentia.upf.edu` on first run,
    cache them (IndexedDB/OPFS or a server cache), and show the CC BY-NC attribution. The
    weights have their own licence and are not AGPL, so mixing them into the source tree
    misstates both;
  - say plainly in the README that the **combined app is non-commercial** — AGPL alone would
    allow commercial use, but the NC weights do not. If someone wants to run it commercially,
    the MTG proprietary licence is the route;
  - the browser-only build is the right default: no server to run, files stay local, and the
    whole thing is one static deployment. Add the Node worker only when users ask to point it
    at a folder.
- **Commercial SaaS (if the decision changes):** keep Essentia server-side (AGPL source
  offer) or replace it, and buy a commercial model licence from MTG (or train your own
  heads). Do not ship `essentia.js` + MTG weights to browsers in a closed product.
- **Commercial, fully permissive (if the decision changes):** this is a real option, at an
  accuracy cost. Beat This ONNX (MIT code and weights) for BPM/beats; a TypeScript chroma +
  profile matcher for key (~60–70 % on EDM is the family ceiling, per §4); CLAP embeddings
  (Apache-2.0 weights) with your own small head for mood; and the DSP energy composite.
  Expect to spend the effort on the key profiles and the CLAP head, and to be behind
  Essentia's multifeature on tempo until the Beat This path is measured.

### 8.3 Accuracy guardrails

- Return `bpm` and `bpm_raw`, `key` and `strength`, and a `confidence` per mood head.
- Fold tempo into 70–180 and show the fold; let the user toggle half/double with one click.
- Treat `strength < 0.5` on key and low beat-interval consistency on tempo as "needs review",
  not as a number to display confidently.
- Store the analyzer/model version with every result; different versions legitimately
  disagree.
- Validate on ~100 real tracks with hand-checked BPM/key before trusting the aggregate
  accuracy (see §9).

### 8.4 Where it fits this repository

Keep the ESP32 a byte server. The models are 3–18 MB and there is no supported ESP-IDF
build of ONNX Runtime (its targets are desktop/mobile/server), so running any of this on the
S3 is a different project; the device already treats PSRAM as a scarce resource
([ADR 0004](../adr/0004-ui-memory-is-external.md)).
The webapp belongs on the host: it can scan the same music folder,
or pull tracks over the device's existing HTTP routes, and write one `analysis.json` per
track (or one library-level file) keyed by relative path plus content hash. If the results
should be visible on the device, the cheapest route is a sidecar JSON endpoint — not a new
field in the boot-built library tree
([s13](server/s13-library-tree-and-mcp.md)), whose scan is already the expensive part.

---

## 9. What is not verified, and the experiments worth running

**Not verified:**

- No real-music accuracy measurement was possible here; the test signal is synthetic and
  only supports timing conclusions.
- Essentia publishes no head-to-head between `multifeature` and `degara`. The docs say
  `degara` is less accurate and faster, and give a confidence calibration only for
  `multifeature`; the size of the accuracy gap is unknown.
- `KeyExtractor`'s own accuracy is unpublished. The 60–70 % figure is for its profile-matching
  family (MIREX abstracts), not for Essentia specifically. MIREX pages were intermittently
  unreachable from here.
- Browser `onnxruntime-web` latency is an estimate: I measured ORT Node and native, not a
  browser. Expect 1.5–3× the single-thread Node numbers for WASM without threads, less with
  `crossOriginIsolated` and multiple threads.
- Beat This ONNX is community work (several MIT conversions and a C++ ORT port), not an
  official export; its CPU latency and accuracy after conversion are unmeasured here.
- The two Essentia licence pages contradict each other on the exact CC variant.
- Whether `AudioContext.decodeAudioData` handles FLAC consistently across Safari/Firefox
  versions (MP3 is safe).

**Experiments, in order:**

1. Build a 100-track ground-truth set (BPM/key from a DJ library or hand-labelled) spanning
   the genres you care about; measure Degara vs multifeature BPM within ±2% and key exact /
   relative accuracy. This is the only way to turn the accuracy claims into numbers.
2. Measure `onnxruntime-web` EffNet on the same set in Chrome with and without COOP/COEP to
   decide whether the browser build needs a server fallback.
3. If the permissive path is on the table, benchmark a Beat This ONNX port on 20 tracks:
   latency versus Degara, and BPM from its beats versus the ground truth. Its accuracy is the
   best documented; its CPU cost is not.
4. Compare DEAM arousal against the computed energy composite on 200 tracks; pick one
   definition and freeze it in the schema.
5. Check memory in the browser: a 4-minute stereo file decoded at 44.1 kHz is ~85 MB of
   float32 before downmix; process one file at a time and release the buffer.

---

## Appendix: commands and model URLs

Measurements were produced with a throwaway Python 3.12 venv (`librosa 1.0.0`,
`essentia 2.1b6`, `onnxruntime 1.31.0`) and a Node 24 scratch project
(`onnxruntime-node 1.30.0`, `essentia.js 0.1.3`). Representative commands:

```bash
# 240 s test material
ffmpeg -f lavfi -i "sine=frequency=220:duration=240" \
       -f lavfi -i "anoisesrc=color=pink:duration=240:sample_rate=44100" \
       -filter_complex "[0:a][1:a]amerge=inputs=2,volume=0.5" -ac 2 -ar 44100 test.wav
ffmpeg -i test.wav -c:a libmp3lame -b:a 320k test.mp3
ffmpeg -i test.wav -c:a flac test.flac

# decode throughput
ffmpeg -y -v error -i test.mp3 -ac 1 -ar 44100 -f f32le /tmp/out.raw

# native Essentia
python -c "import essentia.standard as es; y=es.MonoLoader(filename='test.flac')(); \
print(es.RhythmExtractor2013(method='multifeature')(y)[:2], es.KeyExtractor()(y)[:2])"

# WASM in Node
node -e "const fs=require('fs'); const {EssentiaWASM,Essentia}=require('essentia.js'); \
const b=fs.readFileSync('test-44k.raw'); const pcm=new Float32Array(b.buffer,b.byteOffset,b.length/4); \
const e=new Essentia(EssentiaWASM); const v=e.arrayToVector(pcm); \
console.log(e.BeatTrackerDegara(v), e.KeyExtractor(v,true,4096,4096,12,3500,60,25,0.2,'bgate',44100))"

# ONNX (Node)
node bench.mjs msd-musicnn-1.onnx 187,96 80
node bench.mjs effnet.onnx 128,96 80
```

ONNX model URLs (HTTP 200 verified 2026-10-20):

- `https://essentia.upf.edu/models/feature-extractors/discogs-effnet/discogs-effnet-bsdynamic-1.onnx`
- `https://essentia.upf.edu/models/feature-extractors/musicnn/msd-musicnn-1.onnx`
- `https://essentia.upf.edu/models/classification-heads/mood_happy/mood_happy-msd-musicnn-1.onnx`
- `https://essentia.upf.edu/models/classification-heads/mood_happy/mood_happy-discogs-effnet-1.onnx`
- `https://essentia.upf.edu/models/classification-heads/danceability/danceability-msd-musicnn-1.onnx`
- `https://essentia.upf.edu/models/classification-heads/danceability/danceability-discogs-effnet-1.onnx`
- `https://essentia.upf.edu/models/classification-heads/deam/deam-msd-musicnn-2.onnx`

The `.onnx` variants are not all linked from the catalogue page; they resolve by replacing
`.pb` with `.onnx` in the model URL. Verify each one you ship and record its hash.

---

## Addendum (2026-10-20): what building it changed

The app now exists in [`web/`](../web/README.md). Six findings from the build correct or
sharpen the note above.

1. **EffNet and MusiCNN share one front-end.** Essentia's `TensorflowPredictEffnetDiscogs`
   uses `TensorflowInputMusiCNN` internally (only the patch size differs: 128 frames for
   EffNet, 187 for MusiCNN). There is no separate FSD-SINet front-end for the packaged discogs
   models, so one 16 kHz / 96-band mel path feeds both.
2. **The WASM front-end was the real bottleneck.** Measured at 0.42 ms per 512-sample frame:
   **1.55 s for a 60 s window**, more than either CNN. It is now a TypeScript port of
   Essentia's exact parameters, checked against the WASM implementation in
   `web/tests/mel.test.ts` (max band difference 3.1e-5, mean 3.5e-6). It costs ~0.1 s.
3. **The patch counts in §6.2 were optimistic.** With Essentia's default hops, a 60 s window
   is 47 EffNet patches and 31 MusiCNN patches, not 23 and 8. Measured single-threaded:
   EffNet 0.40 s, MusiCNN 1.23 s. So the realistic browser pipeline for a four-minute track
   is **3–4 s**, not the 2 s estimated in §6.2. The per-patch measurements in §6.1 stand.
4. **DEAM outputs the 1–9 annotation scale**, not 0–1. The app maps `(x − 1) / 8` before
   presenting valence, arousal and energy; clamping the raw output would pin everything at
   1.00.

5. **Tags remove work, not just labels.** On a cache miss the app reads `TBPM`/`TKEY` (ID3)
   and their Vorbis equivalents with `music-metadata`; a tagged BPM or key skips that
   measurement entirely, and when both are tagged no 44.1 kHz decode happens at all. `TMOO` /
   `MOOD` becomes a tagged label (EffNet skipped) and `ENERGY` / `TXXX:ENERGY` becomes tagged
   energy (DEAM skipped); valence/arousal exist only when DEAM runs. Tagged fields are marked
   `source: "metadata"` with null confidence. See ADR-0009.
6. **The analysis can be written back.** `Save tagged copy` writes standard fields
   (`TBPM`/`TKEY`/`TMOO`/`ENERGY`, and their Vorbis equivalents) plus a lossless `XTAPE`
   JSON manifest. A saved copy re-reads in ~93 ms with all six mood probabilities and the
   V/A axes intact, because the manifest takes precedence over the standard tags. Browsers
   cannot overwrite a dropped file, so this is a download, not an in-place edit. See
   ADR-0010.

End-to-end smoke test in headless Chrome on this machine: an 8-second MP3 analysed in 1.35 s
including model initialisation, a cache hit in 38 ms, and the manual deep-tempo path
replacing the fast result. The model set is 24.4 MB: EffNet 18.0 MB, MusiCNN 3.2 MB, six
EffNet tag heads (~0.5 MB each) and DEAM 0.08 MB.
