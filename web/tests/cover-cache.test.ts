import 'fake-indexeddb/auto';
import { openDB } from 'idb';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
  clearCovers,
  clearMedia,
  ensureCover,
  getCoverRecord,
  getMedia,
  listMedia,
  migrateLegacyCovers,
  putMedia,
  type CachedMedia,
  type CoverEncoder,
  type CoverRecord,
} from '../src/storage/media';

const scope = 'mark:cover-cache-test';
const source = new Uint8Array([1, 2, 3, 4, 5]);
const encoded: CoverRecord = { bytes: new Uint8Array([9, 8, 7]), format: 'image/avif', width: 1, height: 1 };

function row(path: string, cover?: { bytes: Uint8Array<ArrayBuffer>; format: string }): CachedMedia {
  return {
    path,
    title: path,
    artist: '',
    album: '',
    genre: '',
    cover,
    at: '2026-11-06T00:00:00Z',
  };
}

beforeEach(async () => {
  await clearMedia(scope);
  await clearCovers();
});

describe('content-addressed covers', () => {
  it('encodes one source once and points every row at it', async () => {
    const encode = vi.fn<CoverEncoder>().mockResolvedValue(encoded);
    const first = await ensureCover({ bytes: source, format: 'image/jpeg' }, encode);
    const second = await ensureCover({ bytes: source, format: 'image/jpeg' }, encode);
    expect(first).toBe(second);
    expect(encode).toHaveBeenCalledTimes(1);

    await putMedia(scope, { ...row('A/one.mp3'), coverHash: first });
    await putMedia(scope, { ...row('A/two.mp3'), coverHash: first });
    const rows = await listMedia(scope);
    expect(rows).toHaveLength(2);
    expect(rows[0].cover?.bytes).toEqual(encoded.bytes);
    expect(rows[1].cover?.bytes).toEqual(encoded.bytes);
    // One object per hash, so a view that makes a Blob URL makes it once per cover.
    expect(rows[0].cover).toBe(rows[1].cover);
  });

  it('does not store the picture on the row when it has a hash', async () => {
    const hash = await ensureCover({ bytes: source, format: 'image/jpeg' }, async () => encoded);
    await putMedia(scope, { ...row('A/one.mp3', { bytes: source, format: 'image/jpeg' }), coverHash: hash });
    const db = await openDB('k7-media', 3);
    const raw = await db.get('media', [scope, 'A/one.mp3']);
    expect(raw?.cover).toBeUndefined();
    expect(raw?.coverHash).toBe(hash);
  });

  it('falls back to the source when the encoder refuses, so the cover is never lost', async () => {
    const hash = await ensureCover(
      { bytes: source, format: 'image/jpeg' },
      async () => {
        throw new Error('no wasm today');
      },
    );
    const record = await getCoverRecord(hash);
    expect(record?.bytes).toEqual(source);
    expect(record?.format).toBe('image/jpeg');
  });
});

describe('legacy rows', () => {
  it('moves raw cover bytes to the shared store and marks the row', async () => {
    const encode = vi.fn<CoverEncoder>().mockResolvedValue(encoded);
    const legacy = { bytes: new Uint8Array([42, 43]), format: 'image/png' as const };
    await putMedia(scope, row('A/legacy.mp3', legacy));

    expect(await migrateLegacyCovers(encode)).toBe(1);
    const migrated = await getMedia(scope, 'A/legacy.mp3');
    expect(migrated?.coverHash).toBeTruthy();
    expect(migrated?.cover?.format).toBe('image/avif');
    expect(encode).toHaveBeenCalledTimes(1);

    const db = await openDB('k7-media', 3);
    const raw = await db.get('media', [scope, 'A/legacy.mp3']);
    expect(raw?.cover).toBeUndefined();
    expect(raw?.coverHash).toBe(migrated?.coverHash);
  });

  it('is idempotent', async () => {
    await putMedia(scope, row('A/legacy.mp3', { bytes: new Uint8Array([42]), format: 'image/png' }));
    expect(await migrateLegacyCovers(async () => encoded)).toBe(1);
    expect(await migrateLegacyCovers(async () => encoded)).toBe(0);
  });
});
