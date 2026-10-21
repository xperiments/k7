import { describe, expect, it } from 'vitest';
import { camelot } from '../src/domain/camelot';

describe('Camelot wheel', () => {
  it('maps all major keys', () => {
    const expected: Record<string, string> = {
      C: '8B',
      G: '9B',
      D: '10B',
      A: '11B',
      E: '12B',
      B: '1B',
      'F#': '2B',
      'C#': '3B',
      'G#': '4B',
      'D#': '5B',
      'A#': '6B',
      F: '7B',
    };
    for (const [tonic, code] of Object.entries(expected)) {
      expect(camelot(tonic, 'major')).toBe(code);
    }
  });

  it('maps all minor keys', () => {
    const expected: Record<string, string> = {
      A: '8A',
      E: '9A',
      B: '10A',
      'F#': '11A',
      'C#': '12A',
      'G#': '1A',
      'D#': '2A',
      'A#': '3A',
      F: '4A',
      C: '5A',
      G: '6A',
      D: '7A',
    };
    for (const [tonic, code] of Object.entries(expected)) {
      expect(camelot(tonic, 'minor')).toBe(code);
    }
  });

  it('treats enharmonic spellings as the same key', () => {
    expect(camelot('Db', 'major')).toBe('3B');
    expect(camelot('Bb', 'minor')).toBe('3A');
    expect(camelot('Gb', 'major')).toBe('2B');
    expect(camelot('Ab', 'minor')).toBe('1A');
  });

  it('pairs relative major and minor on the same wheel number', () => {
    expect(camelot('C', 'major').slice(0, -1)).toBe(camelot('A', 'minor').slice(0, -1));
    expect(camelot('F', 'major').slice(0, -1)).toBe(camelot('D', 'minor').slice(0, -1));
  });

  it('returns empty for an unknown tonic', () => {
    expect(camelot('H', 'major')).toBe('');
  });
});
