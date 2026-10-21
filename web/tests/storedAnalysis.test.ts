import { describe, expect, it } from 'vitest';
import { sampleAnalysis } from './sampleAnalysis';
import {
  analysisTags,
  compactKey,
  dominantMood,
  parseStoredAnalysis,
  toStoredAnalysis,
} from '../src/domain/storedAnalysis';

describe('stored analysis', () => {
  it('round-trips through the XTAPE manifest', () => {
    const stored = toStoredAnalysis(sampleAnalysis);
    const parsed = parseStoredAnalysis(JSON.stringify(stored));

    expect(parsed).toEqual(stored);
    expect(parsed?.moodTags?.relaxed).toBeCloseTo(0.7);
    expect(parsed?.energy).toBeCloseTo(0.6);
  });

  it('writes standard tags plus the manifest', () => {
    const tags = analysisTags(sampleAnalysis);
    expect(tags.BPM).toBe('128.4');
    expect(tags.INITIALKEY).toBe('Am');
    expect(tags.MOOD).toBe('Relaxed');
    expect(tags.ENERGY).toBe('0.6');
    expect(parseStoredAnalysis(tags.XTAPE)?.key.camelot).toBe('8A');
  });

  it('formats compact keys and dominant moods', () => {
    expect(compactKey('A', 'minor')).toBe('Am');
    expect(compactKey('C', 'major')).toBe('C');
    expect(dominantMood(sampleAnalysis.mood.tags)).toBe('Relaxed');
    expect(dominantMood(null)).toBeNull();
  });

  it('rejects manifests it cannot trust', () => {
    expect(parseStoredAnalysis('not json')).toBeNull();
    expect(parseStoredAnalysis(JSON.stringify({ version: 2 }))).toBeNull();
    expect(
      parseStoredAnalysis(
        JSON.stringify({ version: 1, bpm: 120, key: null, moodTags: null }),
      ),
    ).toBeNull();
    expect(
      parseStoredAnalysis(
        JSON.stringify({
          version: 1,
          bpm: 120,
          key: { tonic: 'A', mode: 'minor', camelot: '8A' },
          moodTags: null,
          moodLabel: null,
        }),
      ),
    ).toBeNull();
  });
});
