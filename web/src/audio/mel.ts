/**
 * The log-mel front-end used by the Essentia "MusiCNN" input algorithms
 * (`TensorflowInputMusiCNN`), reimplemented in TypeScript.
 *
 * Reference: essentia/src/algorithms/spectral/tensorflowinputmusicnn.cpp plus
 * windowing.cpp, spectrum.cpp, melbands.cpp and triangularbands.cpp.
 *
 * Parameters, all fixed by training:
 *   16 kHz mono, frame 512, hop 256, symmetric Hann (unnormalised),
 *   512-point magnitude spectrum, 96 Slaney-spaced triangular bands
 *   0–8 kHz with unit_tri normalisation, power spectrum, then
 *   log10(1 + 10000 * energy).
 *
 * `tests/mel.test.ts` asserts these bands match essentia.js within 1e-3.
 */

export const FRAME_SIZE = 512;
export const HOP_SIZE = 256;
export const N_FFT = 512;
export const N_BANDS = 96;
export const SAMPLE_RATE = 16_000;
export const F_MAX = 8_000;

const MIN_LOG_HZ = 1000;
const LIN_SLOPE = 3 / 200;
const MIN_LOG_MEL = MIN_LOG_HZ * LIN_SLOPE;
const LOG_STEP = Math.log(6.4) / 27;

export function hzToMelSlaney(hz: number): number {
  if (hz < MIN_LOG_HZ) return hz * LIN_SLOPE;
  return MIN_LOG_MEL + Math.log(hz / MIN_LOG_HZ) / LOG_STEP;
}

export function melToHzSlaney(mel: number): number {
  if (mel < MIN_LOG_MEL) return mel / LIN_SLOPE;
  return MIN_LOG_HZ * Math.exp((mel - MIN_LOG_MEL) * LOG_STEP);
}

interface FilterBin {
  index: number;
  weight: number;
}

/** 96 triangles, each a sparse list of (spectrum bin, weight). */
export function buildFilterbank(): FilterBin[][] {
  const melLow = hzToMelSlaney(0);
  const melHigh = hzToMelSlaney(F_MAX);
  const step = (melHigh - melLow) / (N_BANDS + 1);
  const freqs = Array.from({ length: N_BANDS + 2 }, (_, i) =>
    melToHzSlaney(melLow + i * step),
  );

  // TriangularBands::createFilters: frequencyScale = (sr / 2) / (inputSize - 1)
  const frequencyScale = F_MAX / (N_FFT / 2);
  const bank: FilterBin[][] = [];

  for (let band = 0; band < N_BANDS; band++) {
    const f1 = freqs[band];
    const f2 = freqs[band + 1];
    const f3 = freqs[band + 2];
    const jBegin = Math.ceil(f1 / frequencyScale);
    const jEnd = Math.floor(f3 / frequencyScale);
    // unit_tri: divide by the theoretical triangle area.
    const norm = (f3 - f1) / 2;
    const bins: FilterBin[] = [];

    for (let j = jBegin; j <= jEnd; j++) {
      const hz = j * frequencyScale;
      const weight = hz < f2 ? (hz - f1) / (f2 - f1) : (f3 - hz) / (f3 - f2);
      bins.push({ index: j, weight: weight / norm });
    }
    bank.push(bins);
  }

  return bank;
}

/** Iterative radix-2 FFT with precomputed tables, sized for 512 points. */
class Fft {
  private readonly cos: Float64Array;
  private readonly sin: Float64Array;
  private readonly reverse: Uint32Array;

  constructor(readonly size: number) {
    this.cos = new Float64Array(size / 2);
    this.sin = new Float64Array(size / 2);
    for (let i = 0; i < size / 2; i++) {
      this.cos[i] = Math.cos((-2 * Math.PI * i) / size);
      this.sin[i] = Math.sin((-2 * Math.PI * i) / size);
    }
    this.reverse = new Uint32Array(size);
    const bits = Math.log2(size);
    for (let i = 0; i < size; i++) {
      let r = 0;
      for (let b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b);
      this.reverse[i] = r;
    }
  }

  /** Writes the power spectrum (|X|^2) of a real frame into `out`. */
  power(input: Float64Array, out: Float64Array): void {
    const n = this.size;
    const re = new Float64Array(n);
    const im = new Float64Array(n);
    for (let i = 0; i < n; i++) re[i] = input[this.reverse[i]];

    for (let len = 2; len <= n; len <<= 1) {
      const half = len >> 1;
      const step = n / len;
      for (let i = 0; i < n; i += len) {
        for (let j = 0; j < half; j++) {
          const k = j * step;
          const c = this.cos[k];
          const s = this.sin[k];
          const a = i + j;
          const b = a + half;
          const tr = re[b] * c - im[b] * s;
          const ti = re[b] * s + im[b] * c;
          re[b] = re[a] - tr;
          im[b] = im[a] - ti;
          re[a] += tr;
          im[a] += ti;
        }
      }
    }

    for (let i = 0; i < n / 2 + 1; i++) out[i] = re[i] * re[i] + im[i] * im[i];
  }
}

const fft = new Fft(N_FFT);
const hann = (() => {
  const w = new Float64Array(FRAME_SIZE);
  for (let i = 0; i < FRAME_SIZE; i++) {
    w[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (FRAME_SIZE - 1));
  }
  return w;
})();
const filterbank = buildFilterbank();

export function frameCount(samples: number): number {
  return samples < FRAME_SIZE ? 0 : Math.floor((samples - FRAME_SIZE) / HOP_SIZE) + 1;
}

/** One 96-band log-mel frame per audio frame. */
export function logMelFrames(pcm: Float32Array | Float64Array): Float32Array[] {
  const frames = frameCount(pcm.length);
  const windowed = new Float64Array(FRAME_SIZE);
  const power = new Float64Array(N_FFT / 2 + 1);
  const out: Float32Array[] = [];

  for (let f = 0; f < frames; f++) {
    const offset = f * HOP_SIZE;
    for (let i = 0; i < FRAME_SIZE; i++) windowed[i] = pcm[offset + i] * hann[i];
    fft.power(windowed, power);

    const bands = new Float32Array(N_BANDS);
    for (let b = 0; b < N_BANDS; b++) {
      let energy = 0;
      const bins = filterbank[b];
      for (let k = 0; k < bins.length; k++) {
        energy += power[bins[k].index] * bins[k].weight;
      }
      bands[b] = Math.log10(1 + 10_000 * energy);
    }
    out.push(bands);
  }

  return out;
}

/**
 * Stacks consecutive band frames into model patches, discarding the tail.
 * Matches Essentia's `patchHopSize` default (93 for MusiCNN, 62 for EffNet).
 */
export function toPatches(
  frames: Float32Array[],
  patchSize: number,
  patchHop: number,
): Float32Array {
  if (frames.length < patchSize) return new Float32Array(0);
  const count = Math.floor((frames.length - patchSize) / patchHop) + 1;
  const patch = new Float32Array(count * patchSize * N_BANDS);

  for (let p = 0; p < count; p++) {
    const start = p * patchHop;
    for (let f = 0; f < patchSize; f++) {
      patch.set(frames[start + f], (p * patchSize + f) * N_BANDS);
    }
  }
  return patch;
}
