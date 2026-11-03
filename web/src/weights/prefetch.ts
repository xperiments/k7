import type { ModelManifest } from '../domain/analysis';
import lock from '../../weights.lock.json';
import type { Weight } from './config';
import { WeightError } from './errors';
import { loadWeightBytes } from './loader';
import { ESSENTIA_WASM, MODEL_MANIFEST, ORT_WASM, TAGLIB_WASM } from './manifest';

/**
 * Fetching the weights ahead of the first analysis, for the installed App.
 *
 * This module is the driver, not the store. Every byte goes through `loadWeightBytes()`,
 * so it is verified against the pinned sha256 and written to the one content-addressed
 * IndexedDB entry; a weight that is already there is verified and returned without a
 * network request. The service worker never sees a weight (O2, O3 of `k7-app.md`).
 *
 * It has no DOM and no React: the caller decides *when* to prefetch (installed, or an
 * explicit Settings action) and renders the progress. Tests inject `load` and `weights`.
 */

export interface PrefetchProgress {
  /** Weights that have been verified and stored. */
  done: number;
  total: number;
  /** The declared size of the weights that are done. */
  bytes: number;
  /** The declared size of the whole manifest. */
  bytesTotal: number;
  /** The weight in flight, or null between weights and at the end. */
  current: string | null;
}

export type WeightLoader = (weight: Weight, signal?: AbortSignal) => Promise<unknown>;

export interface PrefetchOptions {
  /**
   * The weights to prefetch. Defaults to the pinned lock; injected in tests so the order,
   * the totals and a failure are asserted without a 41 MB download.
   */
  weights?: readonly Weight[];
  /** Injected for tests: defaults to the verified IndexedDB loader. */
  load?: WeightLoader;
  onProgress?: (progress: PrefetchProgress) => void;
  signal?: AbortSignal;
}

/** A weight that could not be prefetched. It names the weight and keeps the cause. */
export class PrefetchError extends WeightError {
  constructor(weight: Weight, cause: unknown) {
    const detail = cause instanceof Error && cause.message ? `: ${cause.message}` : '';
    super(`The ${weight.label} weight could not be prefetched${detail}`, weight);
    this.name = 'PrefetchError';
    this.cause = cause;
  }
}

/** The prefetch was cancelled between weights; not a failure of any weight. */
export class PrefetchAbortedError extends Error {
  constructor() {
    super('The weight prefetch was cancelled.');
    this.name = 'AbortError';
  }
}

const LOCAL_PATHS: ReadonlyMap<string, string> = new Map(
  lock.models.map((model) => [model.name, model.localPath]),
);

/**
 * A model is a `Weight` like any other. The manifest owns the set, the hashes and the
 * sizes; the lock owns where the file lives; `weightsUrl` alone turns that into a URL.
 */
function modelWeight(model: ModelManifest['models'][number]): Weight {
  const localPath = LOCAL_PATHS.get(model.name);
  if (!localPath) throw new Error(`weights.lock.json does not list ${model.name}.`);

  return {
    id: `model:${model.name}`,
    label: model.name,
    sha256: model.sha256,
    bytes: model.bytes,
    localPath,
    source: { kind: 'gh', path: `web/public/${localPath}` },
  };
}

/**
 * Every weight the App can need offline: the three runtimes and the nine models, in the
 * order the analyser reaches for them. That is the whole lock file, so the size of the
 * prefetch is the size of the store rather than a second hand-kept list.
 */
export const PREFETCH_WEIGHTS: readonly Weight[] = [
  ESSENTIA_WASM,
  ORT_WASM,
  TAGLIB_WASM,
  ...MODEL_MANIFEST.models.map(modelWeight),
];

/** The exact size of `PREFETCH_WEIGHTS`, from the lock's pinned byte counts. */
export const PREFETCH_BYTES = PREFETCH_WEIGHTS.reduce(
  (total, weight) => total + weight.bytes,
  0,
);

/**
 * Verifies and stores every weight that is not already cached. Resumable: calling it
 * again re-checks the cached bytes and fetches only what is missing, because that is what
 * `loadWeightBytes()` does. Rejects with `PrefetchError` for a weight that failed and
 * `PrefetchAbortedError` when the caller's signal fires.
 */
export async function prefetchWeights(
  options: PrefetchOptions = {},
): Promise<PrefetchProgress> {
  const weights = options.weights ?? PREFETCH_WEIGHTS;
  const load = options.load ?? loadWeightBytes;
  const signal = options.signal;
  const total = weights.length;
  const bytesTotal = weights.reduce((sum, weight) => sum + weight.bytes, 0);

  let done = 0;
  let bytes = 0;

  const report = (current: string | null): void => {
    options.onProgress?.({ done, total, bytes, bytesTotal, current });
  };

  if (signal?.aborted) throw new PrefetchAbortedError();
  report(weights[0]?.id ?? null);

  for (const [index, weight] of weights.entries()) {
    if (signal?.aborted) throw new PrefetchAbortedError();

    try {
      await load(weight, signal);
    } catch (cause) {
      if (signal?.aborted) throw new PrefetchAbortedError();
      throw new PrefetchError(weight, cause);
    }

    // A cancelled run must not claim the weight it was in the middle of as stored.
    if (signal?.aborted) throw new PrefetchAbortedError();

    done += 1;
    bytes += weight.bytes;
    report(weights[index + 1]?.id ?? null);
  }

  return { done, total, bytes, bytesTotal, current: null };
}
