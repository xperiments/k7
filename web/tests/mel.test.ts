import { createRequire } from 'node:module';
import { describe, expect, it } from 'vitest';
import { FRAME_SIZE, HOP_SIZE, logMelFrames } from '../src/audio/mel';

const require = createRequire(import.meta.url);
const { EssentiaWASM, Essentia } = require('essentia.js');

function testSignal(seconds = 2): Float32Array {
  const sr = 16_000;
  const n = sr * seconds;
  const pcm = new Float32Array(n);
  let seed = 0x12345678;
  const noise = () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return seed / 0xffffffff - 0.5;
  };
  for (let i = 0; i < n; i++) {
    const t = i / sr;
    pcm[i] =
      0.32 * Math.sin(2 * Math.PI * 440 * t) +
      0.18 * Math.sin(2 * Math.PI * 1234 * t) +
      0.08 * Math.sin(2 * Math.PI * 61 * t) +
      0.02 * noise();
  }
  return pcm;
}

describe('mel front-end', () => {
  it('matches essentia.js TensorflowInputMusiCNN', () => {
    const pcm = testSignal();
    const ours = logMelFrames(pcm);
    const essentia = new Essentia(EssentiaWASM);

    const frames = Math.min(ours.length, 200);
    expect(frames).toBeGreaterThan(100);

    let maxDiff = 0;
    let total = 0;
    let count = 0;

    for (let f = 0; f < frames; f++) {
      const offset = f * HOP_SIZE;
      const frame = new Float32Array(pcm.subarray(offset, offset + FRAME_SIZE));
      const reference = essentia.vectorToArray(
        essentia.TensorflowInputMusiCNN(essentia.arrayToVector(frame)).bands,
      );
      for (let b = 0; b < ours[f].length; b++) {
        const diff = Math.abs(ours[f][b] - reference[b]);
        maxDiff = Math.max(maxDiff, diff);
        total += diff;
        count++;
      }
    }

    const meanDiff = total / count;
    console.log(`mel parity: max ${maxDiff.toExponential(2)}, mean ${meanDiff.toExponential(2)}`);
    expect(ours[0]).toHaveLength(96);
    expect(maxDiff).toBeLessThan(1e-3);
  });
});
