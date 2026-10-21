import { openDB } from 'idb';
import type { Analysis } from '../domain/analysis';

const dbPromise = openDB('xtape-analysis', 1, {
  upgrade(db) {
    db.createObjectStore('analyses');
  },
});

export async function getCached(key: string): Promise<Analysis | undefined> {
  return (await dbPromise).get('analyses', key);
}

export async function putCached(key: string, analysis: Analysis): Promise<void> {
  await (await dbPromise).put('analyses', analysis, key);
}

export async function clearCache(): Promise<void> {
  await (await dbPromise).clear('analyses');
}
