import { describe, expect, it } from 'vitest';

import { faceIndexFor, faceSeedOf } from '../src/jcard/seed';
import { DEFAULT_META, hueOf } from '../src/ui/album-art';

/**
 * The reference's ten albums, whose pairs reached all five drawn CSS faces before the J-card.
 * The seed is the same hash now; these cases say it still gives one pair one face, that a
 * library reaches every design in a pool, and that the artist is part of the pair.
 */
const REFERENCE: Array<[string, string]> = [
  ['Night Drive', 'Neon Harbor'],
  ['Sunday Tapes', 'Mara Vidal'],
  ['Lo-Fi Vol. 2', 'Various'],
  ['Analog Dreams', 'Ilse Korr'],
  ['Side Project', 'K. Ando'],
  ['Dub Plate 04', 'Sub Halo'],
  ['Midnight FM', 'Radio Varo'],
  ['Glass Coast', 'Ola Brenn'],
  ['Heat Wave', 'Tessa Lund'],
  ['Quiet Engine', 'Ruben Ito'],
];

describe('the coverless face', () => {
  it('gives one title and artist the same design every time', () => {
    for (const [title, artist] of REFERENCE) {
      expect(faceIndexFor(title, artist, 5)).toBe(faceIndexFor(title, artist, 5));
    }
  });

  it('reaches all five designs of a five-design pool across a library', () => {
    const designs = new Set(REFERENCE.map(([title, artist]) => faceIndexFor(title, artist, 5)));
    expect([...designs].sort()).toEqual([0, 1, 2, 3, 4]);
  });

  it('keeps the hue a number the case can use', () => {
    for (const [title] of REFERENCE) {
      const hue = hueOf(title);
      expect(Number.isInteger(hue)).toBe(true);
      expect(hue).toBeGreaterThanOrEqual(0);
      expect(hue).toBeLessThan(360);
    }
  });

  it('takes the artist into the seed', () => {
    const differs = REFERENCE.some(
      ([title, artist]) => faceSeedOf(title, artist) !== faceSeedOf(title, 'Solo'),
    );
    expect(differs).toBe(true);
  });

  it('keeps the pair distinct where a bare concatenation would not', () => {
    expect(faceSeedOf('ab', 'c')).not.toBe(faceSeedOf('a', 'bc'));
  });

  it('has no design when the pool is empty', () => {
    expect(faceIndexFor('Night Drive', 'Neon Harbor', 0)).toBe(-1);
  });

  it('prints the cassette line an album gets', () => {
    expect(DEFAULT_META).toBe('Side A · Side B');
  });
});
