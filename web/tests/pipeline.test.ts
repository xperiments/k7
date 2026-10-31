import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { join } from 'node:path';
import * as ort from 'onnxruntime-node';
import { describe, expect, it } from 'vitest';
import { Analyzer } from '../src/audio/analyzer';
import type { Runtime, TensorData } from '../src/audio/runtime';
import type { ModelManifest } from '../src/domain/analysis';
import { toStoredAnalysis } from '../src/domain/storedAnalysis';
import type { EssentiaModules } from '../src/weights/essentia';

const require = createRequire(import.meta.url);
const { Essentia, EssentiaWASM } = require('essentia.js');

const modelsDir = join(process.cwd(), 'public', 'models');
const hasModels = existsSync(join(modelsDir, 'manifest.json'));

const runtime: Runtime = {
  async createSession(model: Uint8Array) {
    const session = await ort.InferenceSession.create(model, { intraOpNumThreads: 2 });
    return {
      inputNames: session.inputNames,
      outputNames: session.outputNames,
      async run(feeds: Record<string, TensorData>) {
        const inputs: Record<string, ort.Tensor> = {};
        for (const [name, tensor] of Object.entries(feeds)) {
          inputs[name] = new ort.Tensor('float32', tensor.data, [...tensor.dims]);
        }
        const outputs = await session.run(inputs);
        const result: Record<string, TensorData> = {};
        for (const [name, tensor] of Object.entries(outputs)) {
          result[name] = {
            data: tensor.data as Float32Array,
            dims: tensor.dims,
          };
        }
        return result;
      },
    };
  },
};

const loadModel = async (file: string) =>
  new Uint8Array(await readFile(join(modelsDir, file)));

const importEssentia = async (): Promise<EssentiaModules> => ({ Essentia, EssentiaWASM });

/** A 120 BPM click over an A-minor pad, so tempo and key have something to find. */
function clickTrack(sampleRate: number, seconds = 8): Float32Array {
  const pcm = new Float32Array(sampleRate * seconds);
  for (let i = 0; i < pcm.length; i++) {
    const t = i / sampleRate;
    pcm[i] =
      0.04 * Math.sin(2 * Math.PI * 220 * t) +
      0.03 * Math.sin(2 * Math.PI * 261.63 * t) +
      0.03 * Math.sin(2 * Math.PI * 329.63 * t);
  }
  const interval = 0.5;
  for (let beat = 0; beat * interval < seconds; beat++) {
    const start = Math.floor(beat * interval * sampleRate);
    const length = Math.floor(0.06 * sampleRate);
    for (let i = 0; i < length && start + i < pcm.length; i++) {
      const t = i / sampleRate;
      pcm[start + i] += 0.6 * Math.exp(-t * 70) * Math.sin(2 * Math.PI * 90 * t);
    }
  }
  return pcm;
}

describe.skipIf(!hasModels)('analysis pipeline', () => {
  it('runs the real models and returns a complete analysis', async () => {
    const manifest = JSON.parse(
      await readFile(join(modelsDir, 'manifest.json'), 'utf8'),
    ) as ModelManifest;

    const analyzer = new Analyzer(runtime, manifest, loadModel, importEssentia);
    await analyzer.init();

    const duration = 8;
    const analysis = await analyzer.analyze(
      {
        pcm44k: clickTrack(44_100, duration),
        pcm16k: clickTrack(16_000, duration),
        duration,
      },
      { name: 'click.mp3', path: 'album/click.mp3', size: 1234, format: 'mp3', hash: 'test-hash' },
    );

    expect(analysis.schema).toBe(1);
    expect(analysis.tempo.bpm).toBeGreaterThan(90);
    expect(analysis.tempo.bpm).toBeLessThan(150);
    expect(analysis.tempo.confidence).not.toBeNull();
    expect(analysis.tempo.confidence!).toBeGreaterThanOrEqual(0);
    expect(analysis.tempo.confidence!).toBeLessThanOrEqual(1);
    expect(analysis.key.camelot).toMatch(/^\d{1,2}[AB]$/);
    expect(analysis.key.strength).not.toBeNull();
    expect(analysis.key.strength!).toBeGreaterThan(0);

    expect(analysis.mood.tags).not.toBeNull();
    for (const score of Object.values(analysis.mood.tags!)) {
      expect(score).toBeGreaterThanOrEqual(0);
      expect(score).toBeLessThanOrEqual(1);
    }
    expect(analysis.mood.source).toBe('measured');
    expect(analysis.mood.valence).not.toBeNull();
    expect(analysis.mood.arousal).not.toBeNull();
    expect(analysis.energy).toBe(analysis.mood.arousal);
    expect(analysis.energySource).toBe('measured');
    expect(analysis.track.path).toBe('album/click.mp3');
    expect(analysis.provenance.modelSet).toMatch(/^sha256:[0-9a-f]{64}$/);
    expect(analysis.provenance.window).not.toBeNull();
    expect(analysis.provenance.window!.duration).toBe(duration);


    const tagged = await analyzer.analyze(
      { pcm44k: null, pcm16k: clickTrack(16_000, duration), duration },
      {
        name: 'tagged.mp3',
        path: 'tagged.mp3',
        size: 2048,
        format: 'mp3',
        hash: 'tagged-hash',
      },
      {
        bpm: 128,
        key: { tonic: 'A', mode: 'minor', camelot: '8A' },
      },
    );

    expect(tagged.tempo).toMatchObject({
      bpm: 128,
      rawBpm: 128,
      confidence: null,
      engine: null,
      source: 'metadata',
    });
    expect(tagged.key).toMatchObject({
      tonic: 'A',
      mode: 'minor',
      camelot: '8A',
      strength: null,
      source: 'metadata',
    });
    expect(tagged.mood.tags).not.toBeNull();
    expect(tagged.mood.tags!.happy).toBeGreaterThanOrEqual(0);
    expect(tagged.energySource).toBe('measured');

    const fullyTagged = await analyzer.analyze(
      { pcm44k: null, pcm16k: clickTrack(16_000, duration), duration },
      {
        name: 'fully-tagged.mp3',
        path: 'fully-tagged.mp3',
        size: 2048,
        format: 'mp3',
        hash: 'fully-tagged-hash',
      },
      {
        bpm: 100,
        key: { tonic: 'C', mode: 'major', camelot: '8B' },
        mood: 'Energetic',
        energy: 0.8,
      },
    );

    expect(fullyTagged.tempo.source).toBe('metadata');
    expect(fullyTagged.key.source).toBe('metadata');
    expect(fullyTagged.mood).toMatchObject({
      tags: null,
      label: 'Energetic',
      source: 'metadata',
      valence: null,
      arousal: null,
    });
    expect(fullyTagged.energy).toBe(0.8);
    expect(fullyTagged.energySource).toBe('metadata');
    expect(fullyTagged.provenance.window).toBeNull();

    const taggedMoodOnly = await analyzer.analyze(
      { pcm44k: null, pcm16k: clickTrack(16_000, duration), duration },
      {
        name: 'tagged-mood.mp3',
        path: 'tagged-mood.mp3',
        size: 2048,
        format: 'mp3',
        hash: 'tagged-mood-hash',
      },
      {
        bpm: 100,
        key: { tonic: 'C', mode: 'major', camelot: '8B' },
        mood: 'Calm',
      },
    );

    expect(taggedMoodOnly.mood.tags).toBeNull();
    expect(taggedMoodOnly.mood.label).toBe('Calm');
    expect(taggedMoodOnly.mood.source).toBe('metadata');
    expect(taggedMoodOnly.mood.valence).not.toBeNull();
    expect(taggedMoodOnly.energySource).toBe('measured');
    expect(taggedMoodOnly.provenance.window).not.toBeNull();

    const taggedEnergyOnly = await analyzer.analyze(
      { pcm44k: null, pcm16k: clickTrack(16_000, duration), duration },
      {
        name: 'tagged-energy.mp3',
        path: 'tagged-energy.mp3',
        size: 2048,
        format: 'mp3',
        hash: 'tagged-energy-hash',
      },
      {
        bpm: 100,
        key: { tonic: 'C', mode: 'major', camelot: '8B' },
        energy: 0.4,
      },
    );

    expect(taggedEnergyOnly.mood.tags).not.toBeNull();
    expect(taggedEnergyOnly.mood.valence).toBeNull();
    expect(taggedEnergyOnly.mood.arousal).toBeNull();
    expect(taggedEnergyOnly.energy).toBe(0.4);
    expect(taggedEnergyOnly.energySource).toBe('metadata');
    expect(taggedEnergyOnly.provenance.window).not.toBeNull();


    const fromStored = await analyzer.analyze(
      { pcm44k: null, pcm16k: new Float32Array(0), duration },
      {
        name: 'from-stored.mp3',
        path: 'from-stored.mp3',
        size: 2048,
        format: 'mp3',
        hash: 'from-stored-hash',
      },
      { stored: toStoredAnalysis(analysis) },
    );

    expect(fromStored.tempo.source).toBe('metadata');
    expect(fromStored.tempo.bpm).toBeCloseTo(analysis.tempo.bpm, 2);
    expect(fromStored.key.source).toBe('metadata');
    expect(fromStored.key.camelot).toBe(analysis.key.camelot);
    expect(fromStored.mood.source).toBe('metadata');
    expect(fromStored.mood.tags).toEqual(analysis.mood.tags);
    expect(fromStored.energy).toBe(analysis.energy);
    expect(fromStored.energySource).toBe('metadata');
    expect(fromStored.provenance.window).toBeNull();

    const deep = await analyzer.deepTempoOnly({
      pcm44k: clickTrack(44_100, duration),
      pcm16k: new Float32Array(0),
      duration,
    });
    expect(deep.engine).toBe('deep');
    expect(deep.bpm).toBeGreaterThan(60);
  });
});
