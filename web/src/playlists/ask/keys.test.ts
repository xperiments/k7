/** Key spellings the model writes, rewritten to what the device reads. */

import { describe, expect, it } from 'vitest';

import type { SearchArgs } from '../../device/mcp';

import { normalizeKeys } from './keys';

describe('normalizeKeys', () => {
  it('writes a spelled note the device’s way', () => {
    expect(normalizeKeys('B flat major', { key: ['B flat major'] })).toEqual({ key: ['Bb'] });
    expect(normalizeKeys('B flat minor', { key: ['B flat minor'] })).toEqual({ key: ['Bbm'] });
    expect(normalizeKeys('B flat major', { key: ['Bflat'] })).toEqual({ key: ['Bb'] });
    expect(normalizeKeys('B flat major', { key: ['B♭'] })).toEqual({ key: ['Bb'] });
    expect(normalizeKeys('B flat minor', { key: ['B♭m'] })).toEqual({ key: ['Bbm'] });
    expect(normalizeKeys('F sharp minor', { key: ['F sharp minor'] })).toEqual({ key: ['F#m'] });
    expect(normalizeKeys('F sharp minor', { key: ['F♯m'] })).toEqual({ key: ['F#m'] });
    expect(normalizeKeys('B flat major', { key: ['B-flat'] })).toEqual({ key: ['Bb'] });
  });

  it('lets the sentence settle a bare note', () => {
    expect(normalizeKeys('something in F# minor', { key: ['F#'] })).toEqual({ key: ['F#m'] });
    expect(normalizeKeys('something in F# major', { key: ['F#'] })).toEqual({ key: ['F#'] });
    expect(normalizeKeys('something in F#', { key: ['F#'] })).toEqual({ key: ['F#'] });
  });

  it('leaves a key it cannot read, or one the device already reads', () => {
    expect(normalizeKeys('anything', { key: ['8A'] })).toEqual({ key: ['8A'] });
    expect(normalizeKeys('anything', { key: ['E minor'] })).toEqual({ key: ['E minor'] });
    expect(normalizeKeys('H minor', { key: ['H minor'] })).toEqual({ key: ['H minor'] });
    expect(normalizeKeys('B flat major', { key: ['Bb major'] })).toEqual({ key: ['Bb major'] });
  });

  it('keeps an explicit mode over the sentence’s', () => {
    expect(normalizeKeys('B flat minor', { key: ['B flat major'] })).toEqual({ key: ['Bb'] });
  });

  it('dedupes two spellings of one key', () => {
    expect(normalizeKeys('F# minor', { key: ['F#m', 'F♯m'] })).toEqual({ key: ['F#m'] });
  });

  it('returns the same Intent when there is no key', () => {
    const intent: SearchArgs = { bpmMax: 95 };
    expect(normalizeKeys('slow', intent)).toBe(intent);
  });
});
