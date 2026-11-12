import { describe, expect, it } from 'vitest';
import { duplicatesOf, usageOf } from '../src/card/usage';
import type { CachedMedia } from '../src/storage/media';

const row = (overrides: Partial<CachedMedia> = {}): CachedMedia => ({
  path: 'Artist/Album/01.mp3',
  title: 'One',
  artist: 'Artist',
  album: 'Album',
  genre: 'Rock',
  at: '2026-11-12T00:00:00.000Z',
  ...overrides,
});

describe('usageOf', () => {
  it('reads zero for an empty card', () => {
    expect(usageOf([])).toBe(0);
  });

  it('sums the measured bytes and skips rows with no size', () => {
    expect(
      usageOf([
        row({ size: 1_000, hash: 'h1' }),
        row({ size: 250 }),
        row({}), // before measurement: no size, never guessed at
      ]),
    ).toBe(1_250);
  });
});

describe('duplicatesOf', () => {
  it('reads zero when nothing is duplicated', () => {
    expect(duplicatesOf([])).toBe(0);
    expect(
      duplicatesOf([
        row({ path: 'a.mp3', hash: 'h1' }),
        row({ path: 'b.mp3', hash: 'h2' }),
      ]),
    ).toBe(0);
  });

  it('ignores rows without a hash, so an unmeasured row is never a duplicate', () => {
    expect(duplicatesOf([row(), row({ hash: undefined })])).toBe(0);
  });

  it('counts the copies beyond the first for one hash', () => {
    expect(
      duplicatesOf([
        row({ path: 'a.mp3', hash: 'h1' }),
        row({ path: 'b.mp3', hash: 'h1' }),
      ]),
    ).toBe(1);
    expect(
      duplicatesOf([
        row({ path: 'a.mp3', hash: 'h1' }),
        row({ path: 'b.mp3', hash: 'h1' }),
        row({ path: 'c.mp3', hash: 'h1' }),
      ]),
    ).toBe(2);
  });

  it('treats the same hash on the same path as one row, not a duplicate', () => {
    expect(
      duplicatesOf([
        row({ path: 'a.mp3', hash: 'h1' }),
        row({ path: 'a.mp3', hash: 'h1' }),
      ]),
    ).toBe(0);
  });

  it('sums duplicates across several hashes', () => {
    expect(
      duplicatesOf([
        row({ path: 'a.mp3', hash: 'h1' }),
        row({ path: 'b.mp3', hash: 'h1' }),
        row({ path: 'c.mp3', hash: 'h2' }),
        row({ path: 'd.mp3', hash: 'h2' }),
        row({ path: 'e.mp3', hash: 'h3' }),
      ]),
    ).toBe(2);
  });
});
