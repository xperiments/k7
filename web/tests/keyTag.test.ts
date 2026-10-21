import { describe, expect, it } from 'vitest';
import { parseKeyTag } from '../src/domain/keyTag';

describe('key tags', () => {
  it('parses Camelot codes', () => {
    expect(parseKeyTag('8A')).toEqual({ tonic: 'A', mode: 'minor', camelot: '8A' });
    expect(parseKeyTag('8a')).toEqual({ tonic: 'A', mode: 'minor', camelot: '8A' });
    expect(parseKeyTag('12B')).toEqual({ tonic: 'E', mode: 'major', camelot: '12B' });
    expect(parseKeyTag('1A')).toEqual({ tonic: 'G#', mode: 'minor', camelot: '1A' });
  });

  it('parses the note spellings taggers write', () => {
    expect(parseKeyTag('Am')).toEqual({ tonic: 'A', mode: 'minor', camelot: '8A' });
    expect(parseKeyTag('A minor')).toEqual({ tonic: 'A', mode: 'minor', camelot: '8A' });
    expect(parseKeyTag('Amin')).toEqual({ tonic: 'A', mode: 'minor', camelot: '8A' });
    expect(parseKeyTag('F#m')).toEqual({ tonic: 'F#', mode: 'minor', camelot: '11A' });
    expect(parseKeyTag('C major')).toEqual({ tonic: 'C', mode: 'major', camelot: '8B' });
    expect(parseKeyTag('Bb')).toEqual({ tonic: 'Bb', mode: 'major', camelot: '6B' });
    expect(parseKeyTag('Ab min')).toEqual({ tonic: 'Ab', mode: 'minor', camelot: '1A' });
  });

  it('normalises enharmonic note names', () => {
    expect(parseKeyTag('Cb')).toEqual({ tonic: 'B', mode: 'major', camelot: '1B' });
    expect(parseKeyTag('E#m')).toEqual({ tonic: 'F', mode: 'minor', camelot: '4A' });
  });

  it('rejects values it cannot trust', () => {
    expect(parseKeyTag('')).toBeNull();
    expect(parseKeyTag('H')).toBeNull();
    expect(parseKeyTag('13A')).toBeNull();
    expect(parseKeyTag('0B')).toBeNull();
    expect(parseKeyTag('banana')).toBeNull();
  });
});
