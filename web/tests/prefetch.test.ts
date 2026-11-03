import { describe, expect, it } from 'vitest';
import type { Weight } from '../src/weights/config';
import { isWeightError } from '../src/weights/errors';
import {
  ESSENTIA_WASM,
  MODEL_MANIFEST,
  ORT_WASM,
  TAGLIB_WASM,
} from '../src/weights/manifest';
import {
  PREFETCH_BYTES,
  PREFETCH_WEIGHTS,
  PrefetchAbortedError,
  PrefetchError,
  prefetchWeights,
  type PrefetchProgress,
} from '../src/weights/prefetch';

const HASH = 'a'.repeat(64);

function weight(id: string, bytes: number): Weight {
  return {
    id,
    label: `${id}.onnx`,
    sha256: HASH,
    bytes,
    localPath: `models/${id}.onnx`,
    source: { kind: 'gh', path: `web/public/models/${id}.onnx` },
  };
}

/**
 * A stand-in for `loadWeightBytes`: it is the loader that dedupes, so a weight that has
 * already been stored is returned without a network request. `fetched` is therefore the
 * number of real downloads, which a second prefetch must not add to.
 */
interface FakeLoader {
  load: (candidate: Weight) => Promise<Uint8Array>;
  calls: string[];
  fetched: string[];
}

function cacheAwareLoader(failOn?: string): FakeLoader {
  const calls: string[] = [];
  const fetched: string[] = [];
  const stored = new Set<string>();

  return {
    calls,
    fetched,
    async load(candidate) {
      calls.push(candidate.id);
      if (candidate.id === failOn) throw new Error('the network went away');
      if (!stored.has(candidate.id)) {
        fetched.push(candidate.id);
        stored.add(candidate.id);
      }
      return new Uint8Array(0);
    },
  };
}

const THREE = [weight('a', 100), weight('b', 200), weight('c', 300)];

function progressInto(events: PrefetchProgress[]) {
  return (progress: PrefetchProgress) => events.push(progress);
}

describe('prefetching the analysis weights', () => {
  it('fetches every weight in manifest order', async () => {
    const loader = cacheAwareLoader();

    await prefetchWeights({ weights: THREE, load: loader.load });

    expect(loader.calls).toEqual(['a', 'b', 'c']);
    expect(loader.fetched).toEqual(['a', 'b', 'c']);
  });

  it('reports progress against the manifest totals', async () => {
    const loader = cacheAwareLoader();
    const events: PrefetchProgress[] = [];

    const summary = await prefetchWeights({
      weights: THREE,
      load: loader.load,
      onProgress: progressInto(events),
    });

    // 100 + 200 + 300, counted in the declared sizes rather than in whatever a fetch
    // happened to return.
    expect(events).toEqual([
      { done: 0, total: 3, bytes: 0, bytesTotal: 600, current: 'a' },
      { done: 1, total: 3, bytes: 100, bytesTotal: 600, current: 'b' },
      { done: 2, total: 3, bytes: 300, bytesTotal: 600, current: 'c' },
      { done: 3, total: 3, bytes: 600, bytesTotal: 600, current: null },
    ]);
    expect(summary).toEqual({ done: 3, total: 3, bytes: 600, bytesTotal: 600, current: null });
  });

  it('skips what is already cached: a second run downloads nothing', async () => {
    const loader = cacheAwareLoader();

    await prefetchWeights({ weights: THREE, load: loader.load });
    expect(loader.fetched).toEqual(['a', 'b', 'c']);

    const events: PrefetchProgress[] = [];
    await prefetchWeights({
      weights: THREE,
      load: loader.load,
      onProgress: progressInto(events),
    });

    // The verified loader reads each cached weight and re-checks its hash, but no byte
    // crosses the network a second time.
    expect(loader.fetched).toEqual(['a', 'b', 'c']);
    expect(loader.calls).toEqual(['a', 'b', 'c', 'a', 'b', 'c']);
    expect(events.at(-1)).toEqual({
      done: 3,
      total: 3,
      bytes: 600,
      bytesTotal: 600,
      current: null,
    });
  });

  it('stops before the first fetch when the signal is already aborted', async () => {
    const loader = cacheAwareLoader();
    const controller = new AbortController();
    controller.abort();

    await expect(
      prefetchWeights({ weights: THREE, load: loader.load, signal: controller.signal }),
    ).rejects.toBeInstanceOf(PrefetchAbortedError);

    expect(loader.calls).toEqual([]);
    await expect(
      prefetchWeights({ weights: THREE, load: loader.load, signal: controller.signal }),
    ).rejects.toMatchObject({ name: 'AbortError' });
  });

  it('stops after the weight in flight when the signal aborts mid-run', async () => {
    const calls: string[] = [];
    const controller = new AbortController();
    const abortOnA = async (candidate: Weight): Promise<Uint8Array> => {
      calls.push(candidate.id);
      controller.abort();
      return new Uint8Array(0);
    };

    await expect(
      prefetchWeights({ weights: THREE, load: abortOnA, signal: controller.signal }),
    ).rejects.toBeInstanceOf(PrefetchAbortedError);

    expect(calls).toEqual(['a']);
  });

  it('rejects with a typed error naming the weight that failed', async () => {
    const loader = cacheAwareLoader('b');

    const failure: unknown = await prefetchWeights({
      weights: THREE,
      load: loader.load,
    }).catch((error: unknown) => error);

    expect(failure).toBeInstanceOf(PrefetchError);
    expect(failure).toBeInstanceOf(Error);
    const error = failure as PrefetchError;
    expect(error.weight.id).toBe('b');
    expect(error.message).toContain('b.onnx');
    expect(isWeightError(error)).toBe(true);
    expect((error.cause as Error).message).toBe('the network went away');
    // The failure is refused rather than repaired; the third weight is not attempted.
    expect(loader.calls).toEqual(['a', 'b']);
  });
});

describe('the weights the offline app prefetches', () => {
  it('is the three runtimes and the nine pinned models', () => {
    expect(PREFETCH_WEIGHTS).toHaveLength(12);
    expect(PREFETCH_WEIGHTS.slice(0, 3)).toEqual([ESSENTIA_WASM, ORT_WASM, TAGLIB_WASM]);
    expect(PREFETCH_WEIGHTS.slice(3).map((candidate) => candidate.id)).toEqual(
      MODEL_MANIFEST.models.map((model) => `model:${model.name}`),
    );
  });

  it('sizes the download from the lock file, not from a rounded claim', () => {
    // weights.lock.json: 2 562 601 (Essentia) + 14 239 897 (ORT) + 703 477 (TagLib)
    // + 24 362 680 across the nine models.
    expect(PREFETCH_BYTES).toBe(41_868_655);
  });

  it('prefetches the pinned manifest when the caller injects only the loader', async () => {
    const loader = cacheAwareLoader();

    const summary = await prefetchWeights({ load: loader.load });

    expect(loader.calls).toEqual(PREFETCH_WEIGHTS.map((candidate) => candidate.id));
    expect(summary).toEqual({
      done: 12,
      total: 12,
      bytes: PREFETCH_BYTES,
      bytesTotal: PREFETCH_BYTES,
      current: null,
    });
  });
});
