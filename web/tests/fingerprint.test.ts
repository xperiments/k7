import { describe, expect, it } from 'vitest';
import { cardFingerprint, type CardEntry } from '../src/card/fingerprint';

/**
 * Every digest below was computed independently, by `shasum -a 256` over the exact UTF-8
 * bytes named in the test - never by calling the function under test. That is what makes
 * them able to disagree with it.
 *
 * The encoding is pinned here: one line per entry, `path:size:mtime`, sorted by the path's
 * UTF-8 bytes and joined with a newline (no trailing newline). Same walk, same bytes, same
 * fingerprint on any machine.
 */
const EMPTY = 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855';
const ONE = '36d34129610fe8c804744f2600c98c1f1c4231c26e378f545745ee2bab63a595';
const TWO = '9e7fa3562dc401bb7b86facfdbcc32770048f0719668df0207c07826dfe8036e';
const TWO_SIZE_CHANGED = 'd5857efa5651320c79761127b0ff8177b6f4982803e90f62f3bf7e630299ad7c';
const TWO_MTIME_CHANGED = '437e0b937a1a07e360c5abb5abeb1284b37dcc6673770b913b128875b153fb81';
/** Byte order: `Zebra` (0x5a), `apple` (0x61), `Ä` (0xc3 0x84). */
const BYTE_SORTED = 'edb3023c22f5be4075ff7887487fba62fff8ef66e87b26e2f8abd515772cfd02';
/** Byte order puts U+FFFD (0xef…) before U+1F600 (0xf0…), which is not code-unit order. */
const UTF8_SORTED = '51b06925100258f5559b264827e8c4968efb68269b9412bffcc8a5b47d4d604e';

const TWO_ENTRIES: CardEntry[] = [
  { path: 'A/01.mp3', size: 1234, mtime: 1700000000000 },
  { path: 'B/02.flac', size: 42, mtime: 1700000001000 },
];

const BYTE_ORDERED: CardEntry[] = [
  { path: 'apple/02.mp3', size: 20, mtime: 2000 },
  { path: 'Ä/03.mp3', size: 30, mtime: 3000 },
  { path: 'Zebra/01.mp3', size: 10, mtime: 1000 },
];

const UTF8_ORDERED: CardEntry[] = [
  { path: '😀/01.mp3', size: 10, mtime: 1000 },
  { path: '�/02.mp3', size: 42, mtime: 2000 },
];

describe('the card fingerprint', () => {
  it('hashes the path:size:mtime line, and nothing else', async () => {
    expect(await cardFingerprint([{ path: 'music/01.mp3', size: 5, mtime: 7 }])).toBe(ONE);
  });

  it('is the empty hash for a card with no tracks', async () => {
    expect(await cardFingerprint([])).toBe(EMPTY);
  });

  it('is stable across the order the walk produced', async () => {
    const forwards = await cardFingerprint(TWO_ENTRIES);
    const backwards = await cardFingerprint([...TWO_ENTRIES].reverse());
    expect(forwards).toBe(TWO);
    expect(backwards).toBe(TWO);
  });

  it('sorts by path bytes, not by locale', async () => {
    // A locale sort would keep `apple` before `Zebra`; bytes put `Z` first.
    expect(await cardFingerprint(BYTE_ORDERED)).toBe(BYTE_SORTED);
    expect(await cardFingerprint([...BYTE_ORDERED].reverse())).toBe(BYTE_SORTED);
  });

  it('orders supplementary characters by their UTF-8 bytes', async () => {
    expect(await cardFingerprint(UTF8_ORDERED)).toBe(UTF8_SORTED);
    expect(await cardFingerprint([...UTF8_ORDERED].reverse())).toBe(UTF8_SORTED);
  });

  it('changes when a file size changes', async () => {
    const changed = [
      { ...TWO_ENTRIES[0], size: TWO_ENTRIES[0].size + 1 },
      TWO_ENTRIES[1],
    ];
    expect(await cardFingerprint(changed)).toBe(TWO_SIZE_CHANGED);
    expect(await cardFingerprint(changed)).not.toBe(TWO);
  });

  it('changes when a file mtime changes', async () => {
    const changed = [
      { ...TWO_ENTRIES[0], mtime: TWO_ENTRIES[0].mtime + 1 },
      TWO_ENTRIES[1],
    ];
    expect(await cardFingerprint(changed)).toBe(TWO_MTIME_CHANGED);
    expect(await cardFingerprint(changed)).not.toBe(TWO);
  });

  it('changes when a path changes', async () => {
    const renamed = [
      { ...TWO_ENTRIES[0], path: 'A/01 (1).mp3' },
      TWO_ENTRIES[1],
    ];
    expect(await cardFingerprint(renamed)).not.toBe(TWO);
  });

  it('is lowercase hex, 64 characters long', async () => {
    expect(await cardFingerprint(TWO_ENTRIES)).toMatch(/^[0-9a-f]{64}$/);
  });

  it('leaves the caller’s walk in the order it arrived', async () => {
    const entries = [...TWO_ENTRIES].reverse();
    await cardFingerprint(entries);
    expect(entries).toEqual([...TWO_ENTRIES].reverse());
  });

  it('bounds the mtime and size with a separator, not by concatenation', async () => {
    // "1:23" and "12:3" would collide if fields were glued; the colon keeps them apart.
    const left = await cardFingerprint([{ path: 'a', size: 1, mtime: 23 }]);
    const right = await cardFingerprint([{ path: 'a', size: 12, mtime: 3 }]);
    expect(left).not.toBe(right);
  });
});
