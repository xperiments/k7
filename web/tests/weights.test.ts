import { createHash } from 'node:crypto';
import { describe, expect, it } from 'vitest';
import { MODEL_FILES } from '../src/audio/models';
import {
  WEIGHTS_REF,
  WEIGHTS_REPO,
  weightsUrl,
  type Weight,
} from '../src/weights/config';
import { verifySha256 } from '../src/weights/hash';
import { isWeightError, WeightError } from '../src/weights/errors';
import { weightCacheKey } from '../src/weights/key';
import { MODEL_MANIFEST } from '../src/weights/manifest';

const model: Weight = {
  id: 'model:example.onnx',
  label: 'example.onnx',
  sha256: 'a'.repeat(64),
  bytes: 3,
  localPath: 'models/example.onnx',
  source: { kind: 'gh', path: 'web/public/models/example.onnx' },
};

const essentia: Weight = {
  id: 'essentia-wasm',
  label: 'Essentia',
  sha256: 'b'.repeat(64),
  bytes: 3,
  localPath: 'weights/essentia-wasm.es.js',
  source: {
    kind: 'npm',
    package: 'essentia.js',
    path: 'dist/essentia-wasm.es.js',
  },
};

const ABC =
  'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad';

describe('weight URLs', () => {
  it('pins a model to the repository and tag', () => {
    expect(weightsUrl(model, 'cdn')).toBe(
      `https://cdn.jsdelivr.net/gh/${WEIGHTS_REPO}@${WEIGHTS_REF}/web/public/models/example.onnx`,
    );
  });

  it('pins a runtime to its published package version', () => {
    expect(weightsUrl(essentia, 'cdn')).toBe(
      'https://cdn.jsdelivr.net/npm/essentia.js@0.1.3/dist/essentia-wasm.es.js',
    );
  });

  it('resolves a local build against the public tree', () => {
    expect(weightsUrl(model, 'local')).toBe('/models/example.onnx');
    expect(weightsUrl(essentia, 'local')).toBe('/weights/essentia-wasm.es.js');
  });

  it('does not care whether the source path starts with a slash', () => {
    const slashed: Weight = {
      ...model,
      source: { kind: 'gh', path: '/web/public/models/example.onnx' },
    };
    expect(weightsUrl(slashed, 'cdn')).toBe(weightsUrl(model, 'cdn'));
  });
});

describe('sha256 verification', () => {
  it('accepts bytes that hash to the pinned value', async () => {
    expect(await verifySha256(new TextEncoder().encode('abc'), ABC)).toBe(true);
  });

  it('accepts a digest written in uppercase', async () => {
    expect(await verifySha256(new TextEncoder().encode('abc'), ABC.toUpperCase())).toBe(true);
  });

  it('rejects different bytes', async () => {
    expect(await verifySha256(new TextEncoder().encode('abd'), ABC)).toBe(false);
  });

  it('rejects a digest that belongs to something else', async () => {
    expect(await verifySha256(new TextEncoder().encode('abc'), 'c'.repeat(64))).toBe(false);
  });
});

describe('weight cache keys', () => {  const url = 'https://cdn.jsdelivr.net/gh/x/y@v1/web/public/models/example.onnx';

  it('names the URL and the hash, so both identify the entry', () => {
    const key = weightCacheKey(url, 'a'.repeat(64));
    expect(key).toContain(url);
    expect(key).toContain('a'.repeat(64));
  });

  it('is stable for the same URL and hash', () => {
    expect(weightCacheKey(url, 'a'.repeat(64))).toBe(weightCacheKey(url, 'a'.repeat(64)));
  });

  it('changes when the bytes at the URL change', () => {
    expect(weightCacheKey(url, 'a'.repeat(64))).not.toBe(
      weightCacheKey(url, 'b'.repeat(64)),
    );
  });

  it('changes when the URL changes', () => {
    expect(weightCacheKey(url, 'a'.repeat(64))).not.toBe(
      weightCacheKey(`${url}?v=2`, 'a'.repeat(64)),
    );
  });

  it('treats the hash case as the same entry', () => {
    expect(weightCacheKey(url, 'A'.repeat(64))).toBe(weightCacheKey(url, 'a'.repeat(64)));
  });
});

describe('weight failures across a worker boundary', () => {
  it('recognises the class the worker throws', () => {
    expect(isWeightError(new WeightError('no bytes', model))).toBe(true);
  });

  it('recognises the name a structured clone preserves', () => {
    const error = new Error('first download');
    error.name = 'WeightError';
    expect(isWeightError(error)).toBe(true);
  });

  it('does not mistake an ordinary analysis failure for a weight failure', () => {
    expect(isWeightError(new Error('could not decode the file'))).toBe(false);
  });
});

describe('the embedded weights manifest', () => {
  it('pins every model to a sha256 and a size', () => {
    expect(MODEL_MANIFEST.models.length).toBeGreaterThan(0);
    for (const model of MODEL_MANIFEST.models) {
      expect(model.name).toMatch(/\.onnx$/);
      expect(model.sha256).toMatch(/^[0-9a-f]{64}$/);
      expect(model.bytes).toBeGreaterThan(0);
    }
  });

  it('names the model set by a hash, not by a version label', () => {
    expect(MODEL_MANIFEST.modelSet).toMatch(/^sha256:[0-9a-f]{64}$/);
  });

  it('derives the model set from the pinned models', () => {
    const digest = createHash('sha256')
      .update(
        MODEL_MANIFEST.models
          .map((model) => `${model.name}:${model.sha256}`)
          .sort()
          .join('\n'),
      )
      .digest('hex');
    expect(MODEL_MANIFEST.modelSet).toBe(`sha256:${digest}`);
  });

  it('lists each model once', () => {
    const names = MODEL_MANIFEST.models.map((model) => model.name);
    expect(new Set(names).size).toBe(names.length);
  });

  it('names every model the analyser asks for', () => {
    const pinned = new Set(MODEL_MANIFEST.models.map((model) => model.name));
    const used = [
      MODEL_FILES.effnet,
      MODEL_FILES.musicnn,
      MODEL_FILES.deam,
      ...Object.values(MODEL_FILES.heads),
    ];
    for (const name of used) expect(pinned.has(name)).toBe(true);
  });
});
