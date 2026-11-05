import 'fake-indexeddb/auto';
import { describe, expect, it } from 'vitest';
import { writeCurrent, type CachedMedia } from '../src/storage/media';

const row: CachedMedia = {
  path: 'Artist/Album/01.mp3',
  title: 'One',
  artist: 'Artist',
  album: 'Album',
  genre: '',
  at: '2026-01-01T00:00:00.000Z',
  size: 1000,
  mtime: 1000,
  hash: 'ab'.repeat(32),
  written: 3,
};

/**
 * Whether a re-scan may skip the file entirely. The answer is only yes when the row is about
 * these bytes (the hash the page just computed) and a finalize of exactly this writer
 * finished on them. Anything else — another file, an older row from before the field, a
 * build whose tags or label changed — gets the one pass that settles it.
 */
describe('a file that is already settled', () => {
  it('is current only for the same bytes and the same writer', () => {
    expect(writeCurrent(row, row.hash!, 3)).toBe(true);
  });

  it('is not current when the bytes moved', () => {
    expect(writeCurrent(row, 'cd'.repeat(32), 3)).toBe(false);
  });

  it('is not current for a row from before the field existed', () => {
    expect(writeCurrent({ ...row, written: undefined }, row.hash!, 3)).toBe(false);
  });

  it('is not current after the writer’s version moves', () => {
    expect(writeCurrent(row, row.hash!, 4)).toBe(false);
  });

  it('is not current when there is no row at all', () => {
    expect(writeCurrent(undefined, row.hash!, 3)).toBe(false);
  });
});
