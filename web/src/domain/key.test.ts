/** The device's key grammar: Camelot codes and note names, and the wheel both ways. */

import { describe, expect, it } from 'vitest';

import { camelot } from './camelot';
import { camelotName, keyCode } from './key';

describe('keyCode', () => {
  it('reads Camelot and note names the way the device does', () => {
    expect(keyCode('8A')).toBe(8);
    expect(keyCode('8B')).toBe(20);
    expect(keyCode('Am')).toBe(8);
    expect(keyCode('A minor')).toBe(8);
    expect(keyCode('C')).toBe(20);
    expect(keyCode('C#m')).toBe(12);
    expect(keyCode('bogus')).toBe(0);
  });

  it('agrees with the Camelot wheel for all 24 keys', () => {
    const tonics = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
    for (const tonic of tonics) {
      for (const mode of ['major', 'minor'] as const) {
        const code = camelot(tonic, mode);
        const label = `${tonic} ${mode}`;
        expect(code, label).not.toBe('');
        expect(keyCode(code), label).toBeGreaterThan(0);
        expect(camelotName(keyCode(code)), label).toBe(code);
      }
    }
  });
});
