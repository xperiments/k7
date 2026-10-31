import { describe, expect, it } from 'vitest';
import { needsMeasurement, tagAnalysis } from '../src/domain/tagAnalysis';
import { buildSnapshot } from '../src/domain/snapshot';


const track = {
  name: 'a.flac',
  path: 'CARD/music/a.flac',
  size: 100,
  format: 'flac' as const,
  hash: 'x',
};

const tagged = {
  bpm: 174,
  key: { tonic: 'A', mode: 'minor' as const, camelot: '8A' },
};

describe('a track the tags already answer for', () => {
  it('needs no measurement when both BPM and key are tagged', () => {
    expect(needsMeasurement(tagged, true)).toBe(false);
  });

  it('is measured when either is missing, or when the setting is off', () => {
    expect(needsMeasurement({ key: tagged.key }, true)).toBe(true);
    expect(needsMeasurement({ bpm: tagged.bpm }, true)).toBe(true);
    expect(needsMeasurement(tagged, false)).toBe(true);
  });

  it('writes a row from the tags, with what they cannot carry left unknown', () => {
    const bytes = buildSnapshot({
      tracks: [
        { ...track, analysis: tagAnalysis(track, tagged), title: 'A', artist: '', album: '', genre: '' },
      ],
      generationSeconds: 1767225600,
      analyserVersion: 300,
      appSchema: 1,
      modelSet: 0x1234n,
    });
    /* Row layout: fingerprint 0-7, size 8-11, string offset 12-15, bpm 16-17, key 18,
     * energy 19, dance 20, valence 21, arousal 22, mood 23, confidence 24, flags 25. */
    expect(bytes[64 + 25]).toBe(0x03); // tempo and key from metadata
    for (const offset of [19, 20, 21, 22, 24]) {
      expect(bytes[64 + offset]).toBe(0xff); // energy, dance, valence, arousal, confidence
    }
    expect(bytes[64 + 23]).toBe(0); // no mood bits
  });

  it('folds a tag outside the presented range', () => {
    const slow = tagAnalysis(track, { bpm: 60, key: tagged.key });
    expect(slow.tempo.bpm).toBe(120);
  });

  it('keeps a mood tag and marks it as metadata', () => {
    const analysis = tagAnalysis(track, { ...tagged, mood: 'Happy', energy: 0.7 });
    expect(analysis.mood.label).toBe('Happy');
    expect(analysis.mood.source).toBe('metadata');
    expect(analysis.energy).toBe(0.7);
    expect(analysis.energySource).toBe('metadata');
  });
});
