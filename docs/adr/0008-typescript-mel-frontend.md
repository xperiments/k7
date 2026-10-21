# The mel front-end is TypeScript, verified against the WASM reference

The mood models expect log-mel bands, not audio. Essentia ships that front end as
`TensorflowInputMusiCNN`, and `essentia.js` exposes it, so the obvious implementation is to
call it once per 512-sample frame. Measured in Node, that call costs 0.42 ms per frame —
1.55 s for the 60 s mood window, more than both CNN passes together. The same front end is
used by the EffNet and MusiCNN predictors, so this cost cannot be avoided by dropping a model.

## Decision

Implement the front end in TypeScript (`src/audio/mel.ts`) with the parameters read from
Essentia's source: symmetric Hann 512/256, 512-point magnitude spectrum, 96 Slaney-spaced
triangles from 0–8 kHz with `unit_tri` normalisation, power spectrum, then
`log10(1 + 10000·x)`. `tests/mel.test.ts` compares every band against
`essentia.js` `TensorflowInputMusiCNN` on a deterministic signal; the current maximum
difference is 3.1e-5, mean 3.5e-6. The WASM implementation stays in the test as the oracle.

## Considered options

- **Use the WASM front end.** Exact by construction, no porting risk, but adds ~1.5 s per
  track and thousands of JS/WASM calls.
- **Shrink the mood window to 30 s.** Halves the cost without a port, at the price of less
  context for mood and valence.
- **Compute mel inside the ONNX graph.** The published models take mel bands as input, so the
  graph cannot be changed without retraining.

## Consequences

- The front end runs in about 0.1 s for a 60 s window instead of 1.55 s.
- A parity test guards the port. Any Essentia or model upgrade that changes the front end must
  update the constants and the test together; if parity fails, the fallback is the WASM call.
- The implementation only covers the MusiCNN/EffNet parametrisation (16 kHz, 96 bands). The
  FSD-SINet front end (22.05 kHz, different window) is deliberately not implemented because
  the packaged EffNet predictor uses the MusiCNN one.
