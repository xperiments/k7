import { createRequire } from 'node:module';
import { describe, expect, it } from 'vitest';
import { Analyzer } from '../src/audio/analyzer';
import type { Runtime } from '../src/audio/runtime';
import type { ModelManifest } from '../src/domain/analysis';
import type { EssentiaModules } from '../src/weights/essentia';

const require = createRequire(import.meta.url);
const { Essentia, EssentiaWASM } = require('essentia.js') as {
  Essentia: unknown;
  EssentiaWASM: { HEAPU8: Uint8Array };
};

/**
 * Essentia's vectors are allocated in the module's own heap and JavaScript never frees them.
 * Every `arrayToVector` this App makes has to be `.delete()`d, or a long run keeps one
 * excerpt per track — a 120 s window is 21 MB — and the tab dies. That is not a theory: on
 * the old code the heap grew by exactly the excerpt per measure, and the browser then failed
 * `decodeAudioData` for files that were perfectly good ("Unable to decode audio data").
 *
 * This test runs the real Essentia module on a short excerpt with mood and energy tagged, so
 * tempo and key are the only things measured and no ONNX model is loaded.
 */

const manifest: ModelManifest = {
  modelSet: 'test',
  licence: 'test',
  source: 'test',
  models: [],
};

/** Nothing in this test should need a model: fail loudly if one is asked for. */
const runtime: Runtime = {
  createSession() {
    throw new Error('no model session is expected when mood and energy are tagged');
  },
};

const loadModel = async () => {
  throw new Error('no model is expected when mood and energy are tagged');
};

const importEssentia = async (): Promise<EssentiaModules> =>
  ({ Essentia, EssentiaWASM }) as unknown as EssentiaModules;

/** A steady tone with a click every half second, long enough for both measurements. */
function clicks(sampleRate: number, seconds: number): Float32Array {
  const pcm = new Float32Array(sampleRate * seconds);
  for (let i = 0; i < pcm.length; i++) {
    pcm[i] = 0.1 * Math.sin((2 * Math.PI * 220 * i) / sampleRate);
  }
  for (let beat = 0; beat < seconds * 2; beat++) {
    const start = Math.floor(beat * 0.5 * sampleRate);
    for (let i = 0; i < sampleRate * 0.05 && start + i < pcm.length; i++) {
      pcm[start + i] += 0.5 * Math.exp(-i / (sampleRate * 0.01));
    }
  }
  return pcm;
}

const MB = 1024 * 1024;

describe('Essentia memory', () => {
  it('does not keep one 44.1 kHz excerpt per measured track', async () => {
    const analyzer = new Analyzer(runtime, manifest, loadModel, importEssentia);
    await analyzer.init();

    const duration = 8;
    const decoded = {
      pcm44k: clicks(44_100, duration),
      pcm16k: clicks(16_000, duration),
      duration,
    };
    const track = {
      name: 'click.mp3',
      path: 'album/click.mp3',
      size: 1,
      format: 'mp3' as const,
      hash: 'hash',
    };
    const metadata = { mood: 'Calm', energy: 0.5 };

    /* The first measure allocates the algorithm's own working buffers; that growth is not a
     * leak and is not what this test is about. */
    await analyzer.analyze(decoded, track, metadata);
    const before = EssentiaWASM.HEAPU8.length;

    for (let i = 0; i < 10; i++) {
      await analyzer.analyze(decoded, track, metadata);
    }

    const growth = EssentiaWASM.HEAPU8.length - before;
    /* Ten excerpts are 14 MB on the old code. A megabyte of slack lets the allocator keep a
     * block without turning a freed vector into a failure; any real leak is far above it. */
    expect(growth).toBeLessThan(2 * MB);
  }, 60_000);

  it('frees the vector the deep pass borrows too', async () => {
    const analyzer = new Analyzer(runtime, manifest, loadModel, importEssentia);
    await analyzer.init();

    const duration = 8;
    const decoded = {
      pcm44k: clicks(44_100, duration),
      pcm16k: clicks(16_000, duration),
      duration,
    };
    await analyzer.deepTempoOnly(decoded);
    const before = EssentiaWASM.HEAPU8.length;

    for (let i = 0; i < 6; i++) await analyzer.deepTempoOnly(decoded);

    expect(EssentiaWASM.HEAPU8.length - before).toBeLessThan(2 * MB);
  }, 60_000);
});
