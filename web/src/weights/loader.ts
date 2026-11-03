import { openDB } from 'idb';
import { weightsUrl, type Weight } from './config';
import { WeightIntegrityError, WeightUnavailableError } from './errors';
import { verifySha256 } from './hash';
import { weightCacheKey } from './key';

const DB_NAME = 'xtape-weights';
const STORE = 'weights';

let dbPromise: ReturnType<typeof openDB> | null = null;

function database(): ReturnType<typeof openDB> {
  if (!dbPromise) {
    dbPromise = openDB(DB_NAME, 1, {
      upgrade(db) {
        db.createObjectStore(STORE);
      },
    }).catch((error: unknown) => {
      // A browser that refuses IndexedDB still gets a working analyser; caching is an
      // optimisation, so the next call retries rather than keeping a poisoned promise.
      dbPromise = null;
      throw error;
    });
  }
  return dbPromise;
}

async function readCached(key: string): Promise<ArrayBuffer | null> {
  try {
    const db = await database();
    const value: unknown = await db.get(STORE, key);
    return value instanceof ArrayBuffer ? value : null;
  } catch {
    return null;
  }
}

async function removeCached(key: string): Promise<void> {
  try {
    const db = await database();
    await db.delete(STORE, key);
  } catch {
    /* A cache that cannot be written cannot be repaired either. */
  }
}

async function writeCached(key: string, bytes: ArrayBuffer): Promise<void> {
  try {
    const db = await database();
    await db.put(STORE, bytes, key);
  } catch {
    /* Caching is an optimisation; a blocked IndexedDB must not stop an analysis. */
  }
}

/**
 * Loads one weight with the only contract that matters: the bytes handed back hash to the
 * pinned value. The IndexedDB entry is keyed by URL and hash, so a changed value is a miss
 * rather than a stale hit, and a cached entry is verified again before it is trusted.
 */
export async function loadWeightBytes(weight: Weight): Promise<Uint8Array> {
  const url = weightsUrl(weight);
  const key = weightCacheKey(url, weight.sha256);

  const cached = await readCached(key);
  if (cached && (await verifySha256(cached, weight.sha256))) {
    return new Uint8Array(cached);
  }
  if (cached) await removeCached(key);

  let response: Response;
  try {
    // One offline store, and it is the one above: the worker caches the shell and nothing
    // else (O2). Fetching `no-store` keeps a stale HTTP cache from bypassing the hash check.
    response = await fetch(url, { cache: 'no-store' });
  } catch {
    throw new WeightUnavailableError(weight);
  }

  if (!response.ok) throw new WeightUnavailableError(weight);

  const bytes = await response.arrayBuffer();
  if (bytes.byteLength !== weight.bytes || !(await verifySha256(bytes, weight.sha256))) {
    throw new WeightIntegrityError(weight);
  }

  await writeCached(key, bytes);
  return new Uint8Array(bytes);
}

/** The text form is used for JavaScript modules, which are imported from a blob URL. */
export async function loadWeightText(weight: Weight): Promise<string> {
  const bytes = await loadWeightBytes(weight);
  return new TextDecoder().decode(bytes);
}
