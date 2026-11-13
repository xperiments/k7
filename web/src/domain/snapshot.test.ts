import { describe, expect, it } from 'vitest';
import { ANALYSIS_SCHEMA, type Analysis, type MoodTag } from './analysis';
import { buildProfile, type SnapshotTrack } from './snapshot';

/** One measured Track, with only the fields the profile reads varied. */
function track(
  genre: string,
  overrides: {
    bpm?: number;
    camelot?: string;
    energy?: number | null;
    danceable?: number;
    valence?: number | null;
    arousal?: number | null;
    moods?: Partial<Record<Exclude<MoodTag, 'danceable'>, number>>;
  } = {},
): SnapshotTrack {
  const {
    bpm = 120,
    camelot = '8A',
    energy = 0.5,
    danceable = 0.5,
    valence = 0.5,
    arousal = 0.5,
    moods = {},
  } = overrides;
  const tags: Record<MoodTag, number> = {
    happy: 0.1,
    sad: 0.1,
    aggressive: 0.1,
    relaxed: 0.1,
    party: 0.1,
    danceable,
  };
  for (const [mood, value] of Object.entries(moods)) tags[mood as MoodTag] = value ?? 0;
  const analysis: Analysis = {
    schema: ANALYSIS_SCHEMA,
    track: { name: 'a.mp3', path: 'music/a.mp3', size: 1, format: 'mp3', duration: 1, hash: 'h' },
    tempo: {
      bpm,
      rawBpm: bpm,
      confidence: 0.9,
      engine: 'fast',
      escalated: false,
      suggestEscalation: false,
      source: 'measured',
    },
    key: { tonic: 'A', mode: 'minor', camelot, strength: 0.5, source: 'measured' },
    mood: { tags, label: null, source: 'measured', valence, arousal },
    energy,
    energySource: 'measured',
    provenance: {
      analyzer: 'a',
      modelSet: 'm',
      window: null,
      analysedAt: '2026-01-01T00:00:00Z',
    },
  };
  return { path: 'music/a.mp3', size: 1, analysis, title: 'a', artist: '', album: '', genre };
}

describe('buildProfile', () => {
  it('counts the tracks, genres and keys the library actually has', () => {
    const profile = buildProfile([
      track('Techno', { camelot: '8A' }),
      track('techno', { camelot: '9B' }),
      track('Ambient', { camelot: '8A' }),
      track('', { camelot: 'x' }),
    ]);
    expect(profile.tracks).toBe(4);
    expect(profile.genres).toEqual(['Techno', 'Ambient']);
    expect(profile.keys).toEqual(['8A', '9B']);
  });

  it('summarises each axis with its own range and quartiles', () => {
    const profile = buildProfile([
      track('x', { bpm: 100, energy: 0.2, danceable: 0.3, valence: 0.2, arousal: 0.1 }),
      track('x', { bpm: 120, energy: 0.5, danceable: 0.5, valence: 0.5, arousal: 0.5 }),
      track('x', { bpm: 140, energy: 0.9, danceable: 0.8, valence: 0.9, arousal: 0.9 }),
    ]);
    expect(profile.bpm).toEqual({ min: 100, p25: 120, p50: 120, p75: 140, max: 140 });
    expect(profile.energy).toEqual({ min: 20, p25: 50, p50: 50, p75: 90, max: 90 });
    expect(profile.danceability).toEqual({ min: 30, p25: 50, p50: 50, p75: 80, max: 80 });
    expect(profile.valence).toEqual({ min: 20, p25: 50, p50: 50, p75: 90, max: 90 });
    expect(profile.arousal).toEqual({ min: 10, p25: 50, p50: 50, p75: 90, max: 90 });
  });

  it('lists the moods the rows carry at the Snapshot threshold', () => {
    const profile = buildProfile([
      track('x', { moods: { happy: 0.8, party: 0.9 } }),
      track('x', { moods: { happy: 0.4 } }),
    ]);
    expect(profile.moods).toEqual(['happy', 'party']);
  });

  it('has no axis when nothing was measured', () => {
    const profile = buildProfile([
      track('x', { energy: null, valence: null, arousal: null }),
    ]);
    expect(profile.energy).toBeNull();
    expect(profile.valence).toBeNull();
    expect(profile.arousal).toBeNull();
  });
});
