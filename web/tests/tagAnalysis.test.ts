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
    /* Format §4: the 80-byte header comes first, then 32-byte rows. A row's
     * measurements are one little-endian uint64 at offset 24: bpm_code 0..10,
     * key 11..15, energy 16..22, danceability 23..29, valence 30..36,
     * arousal 37..43, confidence 44..50, mood_bits 51..55, source_flags 56..61. */
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const word = view.getBigUint64(80 + 24, true);
    expect(Number(word & 0x7ffn) + 699).toBe(1740); // 174 BPM, tagged
    expect(Number((word >> 11n) & 0x1fn)).toBe(8); // 8A
    for (const shift of [16n, 23n, 30n, 37n, 44n]) {
      expect(Number((word >> shift) & 0x7fn)).toBe(127); // energy, dance, valence, arousal, confidence
    }
    expect(Number((word >> 51n) & 0x1fn)).toBe(0); // no mood bits
    expect(Number((word >> 56n) & 0x3fn)).toBe(0x03); // tempo and key from metadata
  });

  it('folds a tag outside the presented range', () => {
    const slow = tagAnalysis(track, { bpm: 60, key: tagged.key });
    expect(slow.tempo.bpm).toBe(120);
  });

  it('keeps the XTAPE record on the quick path', () => {
    const analysis = tagAnalysis(track, {
      bpm: 174,
      key: tagged.key,
      stored: {
        version: 1,
        bpm: 174,
        key: tagged.key,
        moodTags: {
          happy: 0.1,
          sad: 0.05,
          aggressive: 0.1,
          relaxed: 0.7,
          party: 0.03,
          danceable: 0.02,
        },
        moodLabel: 'Relaxed',
        valence: 0.6,
        arousal: 0.4,
        energy: 0.5,
      },
    });
    expect(analysis.mood.tags?.relaxed).toBeCloseTo(0.7);
    expect(analysis.mood.label).toBe('Relaxed');
    expect(analysis.mood.source).toBe('metadata');
    expect(analysis.mood.valence).toBeCloseTo(0.6);
    expect(analysis.mood.arousal).toBeCloseTo(0.4);
    expect(analysis.energy).toBeCloseTo(0.5);
    expect(analysis.energySource).toBe('metadata');
  });

  it('keeps a mood tag and marks it as metadata', () => {
    const analysis = tagAnalysis(track, { ...tagged, mood: 'Happy', energy: 0.7 });
    expect(analysis.mood.label).toBe('Happy');
    expect(analysis.mood.source).toBe('metadata');
    expect(analysis.energy).toBe(0.7);
    expect(analysis.energySource).toBe('metadata');
  });
});
