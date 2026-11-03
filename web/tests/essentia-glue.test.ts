import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { describe, expect, it } from 'vitest';
import { EssentiaGlueError, patchEssentiaGlue } from '../src/weights/essentia-glue';

const require = createRequire(import.meta.url);
const { Essentia } = require('essentia.js');

/**
 * The pinned glue the App actually loads (`scripts/prepare-assets.mjs` copies this file into
 * `public/weights`, and the manifest's hash pins it). Reading the real bytes is the point:
 * a rewrite tested against a stand-in would prove nothing about the module that ships.
 */
const GLUE = join(process.cwd(), 'public', 'weights', 'essentia-wasm.es.js');

/** Where the two variants are written so they can be imported as modules. */
const DIR = join(process.cwd(), 'node_modules', '.cache', 'k7-essentia-glue');

/**
 * The glue's Node branch reads `__dirname`, a CommonJS global. The browser never takes that
 * branch — it is only here so the module can be instantiated in this test at all.
 */
const PRELUDE = 'const __dirname = ".";\n';

async function importVariant(name: string, text: string): Promise<unknown> {
  await mkdir(DIR, { recursive: true });
  const file = join(DIR, `${name}.mjs`);
  await writeFile(file, PRELUDE + text);
  const loaded = (await import(/* @vite-ignore */ pathToFileURL(file).href)) as {
    EssentiaWASM?: unknown;
  };
  return loaded.EssentiaWASM;
}

/** A 120 BPM click over an A-minor pad: tempo, key and beats all have something to find. */
function signal(): Float32Array {
  const sampleRate = 16_000;
  const seconds = 6;
  const pcm = new Float32Array(sampleRate * seconds);
  for (let i = 0; i < pcm.length; i += 1) {
    const t = i / sampleRate;
    pcm[i] =
      0.05 * Math.sin(2 * Math.PI * 220 * t) +
      0.04 * Math.sin(2 * Math.PI * 261.63 * t) +
      0.04 * Math.sin(2 * Math.PI * 329.63 * t);
  }
  for (let beat = 0; beat * 0.5 < seconds; beat += 1) {
    const at = beat * 0.5 * sampleRate;
    for (let i = 0; i < 2200 && at + i < pcm.length; i += 1) {
      pcm[at + i] += 0.4 * Math.exp(-i / 800);
    }
  }
  return pcm;
}

/** What the analyser asks the module for, on one signal, as plain numbers. */
function measure(wasm: unknown) {
  const essentia = new Essentia(wasm);
  const pcm = signal();
  const key = essentia.KeyExtractor(essentia.arrayToVector(pcm));
  const rhythm = essentia.RhythmExtractor2013(essentia.arrayToVector(pcm));
  const beats = essentia.BeatTrackerDegara(essentia.arrayToVector(pcm));
  return {
    key: key.key,
    scale: key.scale,
    strength: Number(key.strength),
    bpm: Number(rhythm.bpm),
    beatCount: Number(beats.ticks.size()),
  };
}

describe('Essentia under a policy without unsafe-eval', () => {
  it('rewrites every dynamic-code site out of the pinned glue', async () => {
    const glue = await readFile(GLUE, 'utf8');
    // The problem, stated on the bytes that ship. If this line ever fails, the pinned build
    // stopped using dynamic code and this module can be deleted.
    expect(glue).toContain('new Function');

    const patched = patchEssentiaGlue(glue);

    expect(patched).not.toContain('new Function');
    expect(patched).not.toContain('eval(');
    // The three bodies written out, and the module otherwise intact: a rewrite, not a stub.
    expect(patched).toContain('return dynCall.apply(null,a)');
    expect(patched).toContain('Reflect.construct(constructor,a)');
    expect(patched.length).toBeLessThan(glue.length);
  });

  it('measures exactly what the unpatched glue measures', async () => {
    const glue = await readFile(GLUE, 'utf8');
    const [patched, baseline] = await Promise.all([
      importVariant('patched', patchEssentiaGlue(glue)),
      importVariant('baseline', glue),
    ]);

    const expected = measure(baseline);
    // A sanity check on the fixture itself: a silent signal would compare two zeros.
    expect(expected.beatCount).toBeGreaterThan(4);
    expect(expected.strength).toBeGreaterThan(0);
    expect(measure(patched)).toEqual(expected);
  }, 120_000);

  it('refuses a glue it does not know rather than loading dynamic code', () => {
    expect(() => patchEssentiaGlue('function somethingElse(){}')).toThrow(EssentiaGlueError);
    expect(() => patchEssentiaGlue('function somethingElse(){}')).toThrow(/createNamedFunction/);
  });
});
