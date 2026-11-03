import 'fake-indexeddb/auto';
import { describe, expect, it, vi } from 'vitest';
import { measuredHash } from '../src/storage/media';

/**
 * The workflow the library views depend on, as one run:
 *
 *   pick a card (a **Scope**) -> measure it (k7-media rows) -> build and send an image
 *   (k7-snapshots) -> **reload the page** -> open Media and Playlists.
 *
 * After the reload the card is in the player, so the App cannot pick it again to learn its
 * scope. Nothing in the Media or Playlists areas can show what this browser measured unless
 * the scope survives the reload, which is the fact these tests pin.
 *
 * The modules are re-imported with `vi.resetModules()` so module-level state is rebuilt the
 * way a page load rebuilds it; only IndexedDB persists, as it really does.
 */

async function loadApp() {
  const session = await import('../src/card/session');
  const media = await import('../src/storage/media');
  const snapshots = await import('../src/storage/snapshots');
  const albums = await import('../src/media/albums');
  return { session, media, snapshots, albums };
}

/**
 * The browser's `localStorage`, which is the one part of the page that survives a reload:
 * `vi.resetModules()` rebuilds every module-level value and leaves this alone, exactly as a
 * real page load does. Installed before the first import because `card/session.ts` reads it
 * when it first loads.
 */
const stored = new Map<string, string>();
Object.defineProperty(globalThis, 'localStorage', {
  configurable: true,
  value: {
    getItem: (key: string) => stored.get(key) ?? null,
    setItem: (key: string, value: string) => void stored.set(key, String(value)),
    removeItem: (key: string) => void stored.delete(key),
    clear: () => stored.clear(),
  },
});

/** A page load: every module-level value (`scope` included) starts over. */
async function reload(): Promise<ReturnType<typeof loadApp>> {
  vi.resetModules();
  return loadApp();
}

function row(path: string, album: string) {
  return {
    path,
    title: path.split('/').pop() ?? path,
    artist: 'Aphex Twin',
    album,
    genre: 'IDM',
    at: new Date().toISOString(),
  };
}

describe('a measured card after the page reloads', () => {
  it('keeps the card scope the rows were written under', async () => {
    const first = await loadApp();
    first.session.setCardScope('mark:00000000-0000-4000-8000-00000000a001');

    const next = await reload();
    expect(next.session.currentCardScope()).toBe('mark:00000000-0000-4000-8000-00000000a001');
  });

  it('lists the albums the browser measured', async () => {
    const first = await loadApp();
    const scope = 'mark:00000000-0000-4000-8000-00000000a002';
    first.session.setCardScope(scope);
    await first.media.putMedia(scope, row('Aphex Twin/Selected Ambient Works/01.mp3', 'Selected Ambient Works'));
    await first.media.putMedia(scope, row('Aphex Twin/Selected Ambient Works/02.mp3', 'Selected Ambient Works'));
    await first.media.putMedia(scope, row('Aphex Twin/Richard D. James/01.mp3', 'Richard D. James'));

    const next = await reload();
    const cache = new Map(
      (await next.media.listMedia(next.session.currentCardScope())).map(
        (entry) => [entry.path, entry] as const,
      ),
    );
    const index = next.albums.buildMediaIndex(cache, null);

    expect(index.albums.map((album) => album.album).sort()).toEqual([
      'Richard D. James',
      'Selected Ambient Works',
    ]);
    expect(index.albums.find((album) => album.album === 'Selected Ambient Works')?.tracks).toBe(2);
  });

  it('finds the last image, so Playlists can send a list change', async () => {
    const first = await loadApp();
    const scope = 'mark:00000000-0000-4000-8000-00000000a003';
    first.session.setCardScope(scope);
    await first.snapshots.saveSnapshot(scope, {
      scope,
      card: { mark: '00000000-0000-4000-8000-00000000a003' },
      bytes: new Uint8Array([1, 2, 3]),
      tracks: 3,
      skipped: 0,
      at: new Date().toISOString(),
    });

    const next = await reload();
    const stored = await next.snapshots.loadSnapshot(next.session.currentCardScope());

    expect(stored?.tracks).toBe(3);
  });

  it('remembers a fingerprinted card too', async () => {
    const first = await loadApp();
    first.session.setCardScope(`fingerprint:${'ab'.repeat(32)}`);

    const next = await reload();
    expect(next.session.currentCardScope()).toBe(`fingerprint:${'ab'.repeat(32)}`);
  });

  it('keeps a restored scope to its own card', async () => {
    const first = await loadApp();
    const cardA = 'mark:00000000-0000-4000-8000-00000000a004';
    const cardB = 'mark:00000000-0000-4000-8000-00000000a005';
    first.session.setCardScope(cardA);
    await first.media.putMedia(cardA, row('Aphex Twin/Selected Ambient Works/01.mp3', 'Selected Ambient Works'));

    const next = await reload();
    expect(await next.media.listMedia(cardB)).toEqual([]);
    expect((await next.media.listMedia(next.session.currentCardScope())).map((entry) => entry.path))
      .toEqual(['Aphex Twin/Selected Ambient Works/01.mp3']);
  });

  it('notifies the views when a pick changes the card', async () => {
    const app = await loadApp();
    let seen = 0;
    const stop = app.session.subscribeCardScope(() => {
      seen += 1;
    });
    app.session.setCardScope('mark:00000000-0000-4000-8000-00000000a006');
    app.session.setCardScope('mark:00000000-0000-4000-8000-00000000a006');
    stop();

    expect(seen).toBe(1);
  });

  it('ignores a stored scope this build did not write', async () => {
    stored.set('k7.scope.v1', 'something-else');

    const next = await reload();
    expect(next.session.currentCardScope()).toBe('local');
  });

  it('recovers the card from the newest image when nothing was stored', async () => {
    // The state a browser is in having measured a card before the scope was remembered:
    // the rows and the image are there, and nothing names the card they belong to.
    const first = await loadApp();
    const scope = 'mark:00000000-0000-4000-8000-00000000a007';
    first.session.setCardScope(scope);
    await first.media.putMedia(scope, row('Aphex Twin/Selected Ambient Works/01.mp3', 'Selected Ambient Works'));
    await first.snapshots.saveSnapshot(scope, {
      scope,
      card: { mark: '00000000-0000-4000-8000-00000000a007' },
      bytes: new Uint8Array([1]),
      tracks: 1,
      skipped: 0,
      at: new Date().toISOString(),
    });

    stored.clear();
    const next = await reload();
    expect(next.session.currentCardScope()).toBe('local');

    await next.session.restoreCardScope();

    expect(next.session.currentCardScope()).toBe(scope);
    const cache = new Map(
      (await next.media.listMedia(next.session.currentCardScope())).map(
        (entry) => [entry.path, entry] as const,
      ),
    );
    expect(next.albums.buildMediaIndex(cache, null).albums).toHaveLength(1);
  });

  it('recognises a file it already measured from the row the card left', async () => {
    const app = await loadApp();
    const scope = 'mark:00000000-0000-4000-8000-00000000a008';
    app.session.setCardScope(scope);
    const path = 'Aphex Twin/Selected Ambient Works/01.mp3';
    const hash = 'ab'.repeat(32);
    await app.media.putMedia(scope, {
      ...row(path, 'Selected Ambient Works'),
      size: 5_242_880,
      mtime: 1_700_000_000_000,
      hash,
    });

    const stored = await app.media.getMedia(scope, path);
    const sameFile = { size: 5_242_880, lastModified: 1_700_000_000_000 };

    // Read off the record: no bytes are read to decide this.
    expect(measuredHash(stored, sameFile)).toBe(hash);
    // The same path with a different size is a different file, and is measured again.
    expect(measuredHash(stored, { ...sameFile, size: 6_000_000 })).toBeNull();
    // And a row from before the fields existed says nothing rather than guessing.
    expect(measuredHash({ ...stored!, hash: undefined }, sameFile)).toBeNull();
  });

  it('gives a skipped row its tags back, so the genre survives a rescan', async () => {
    const app = await loadApp();
    const scope = 'mark:00000000-0000-4000-8000-00000000a009';
    app.session.setCardScope(scope);
    const path = 'Aphex Twin/Selected Ambient Works/01.mp3';
    await app.media.putMedia(scope, {
      ...row(path, 'Selected Ambient Works'),
      size: 1,
      mtime: 2,
      hash: 'cd'.repeat(32),
    });

    const stored = await app.media.getMedia(scope, path);
    // A skipped file is never opened, so this is the only place its genre can come from.
    expect(app.media.tagsOf(stored!)).toEqual({
      title: path.split('/').pop(),
      artist: 'Aphex Twin',
      album: 'Selected Ambient Works',
      genre: 'IDM',
    });
  });
});
