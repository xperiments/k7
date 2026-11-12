import { describe, expect, it } from 'vitest';

import { coverTemplateOf, hueOf } from '../src/ui/album-art';

/**
 * The reference's ten albums, whose pairs reach all five faces. This is the seed table the
 * drawn cover is built from; a change that collapses two faces, or makes one album change
 * its face between visits, shows up here before it shows up on the shelf.
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

describe('the drawn cover', () => {
  it('gives one title and artist the same face every time', () => {
    for (const [title, artist] of REFERENCE) {
      expect(coverTemplateOf(title, artist)).toBe(coverTemplateOf(title, artist));
    }
  });

  it('reaches all five faces across a library', () => {
    const faces = new Set(REFERENCE.map(([title, artist]) => coverTemplateOf(title, artist)));
    expect([...faces].sort()).toEqual([1, 2, 3, 4, 5]);
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
      ([title, artist]) => coverTemplateOf(title, artist) !== coverTemplateOf(title, 'Solo'),
    );
    expect(differs).toBe(true);
  });
});
