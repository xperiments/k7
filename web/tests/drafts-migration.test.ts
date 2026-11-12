import 'fake-indexeddb/auto';
import { describe, expect, it, vi } from 'vitest';

/**
 * A draft authored before scopes existed had no card to belong to. The store files it under
 * `local` — the no-card bucket — rather than discarding it, because a draft is authored
 * content and the honest home for "no card" is the same bucket an unidentified directory gets.
 */

/** A v1 draft row: a list of paths and nothing else, no card scope. */
type LegacyRow = { id: string; name: string; createdAt: number; rows: string[] };

/** Seed the v1 store the way the old build wrote it: one store keyed by id, no scope. */
function seedLegacy(rows: LegacyRow[]) {
  return new Promise<void>((resolve, reject) => {
    const request = indexedDB.open('k7-playlists', 1);
    request.onupgradeneeded = () => {
      const store = request.result.createObjectStore('drafts', { keyPath: 'id' });
      for (const row of rows) store.put(row);
    };
    request.onsuccess = () => {
      request.result.close();
      resolve();
    };
    request.onerror = () => reject(request.error);
  });
}

describe('a draft from before scopes existed', () => {
  it('is filed under local, as one track block per row', async () => {
    await seedLegacy([{ id: 'a'.repeat(16), name: 'Old mix', createdAt: 1, rows: ['a.mp3'] }]);

    vi.resetModules();
    const drafts = await import('../src/playlists/drafts');
    await drafts.loadDrafts();

    const listed = drafts.listDrafts();
    expect(listed.map((draft) => draft.name)).toEqual(['Old mix']);
    expect(listed[0].rows).toEqual(['a.mp3']);
    expect(listed[0].entries.map((entry) => entry.kind)).toEqual(['track']);
  });
});
