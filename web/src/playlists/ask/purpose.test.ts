/**
 * The table and the normalizer, without a model: the row a sentence names, and the filters it
 * keeps.
 */

import { describe, expect, it } from 'vitest';

import type { SearchArgs } from '../../device/mcp';

import { normalizePurpose, purposeFilters } from './purpose';
import { TEST_PROFILE as PROFILE } from './test-support';

describe('purposeFilters', () => {
  it('reads the table at the profile, in the five languages', () => {
    expect(purposeFilters('10 songs for a workout', null)).toEqual({
      energyMin: 70,
      arousalMin: 60,
    });
    expect(purposeFilters('something for the gym', null)).toEqual({
      energyMin: 70,
      arousalMin: 60,
    });
    expect(purposeFilters('party playlist', null)).toEqual({
      mood: ['party'],
      danceabilityMin: 70,
    });
    expect(purposeFilters('de la musique calme pour lire', null)).toEqual({
      mood: ['relaxed'],
      energyMax: 40,
    });
    expect(purposeFilters('música triste', null)).toBeNull();
    expect(purposeFilters('sad and slow', null)).toBeNull();
    expect(purposeFilters('slow', null)).toBeNull();
    expect(purposeFilters('workout', PROFILE)).toEqual({ energyMin: 74, arousalMin: 73 });
  });
});

describe('normalizePurpose', () => {
  const heavy: SearchArgs = {
    bpmMin: 130,
    bpmMax: 160,
    energyMin: 70,
    energyMax: 90,
    danceabilityMin: 70,
    danceabilityMax: 90,
    valenceMin: 60,
    valenceMax: 80,
    arousalMin: 60,
    arousalMax: 80,
    sort: 'bpm',
  };

  it('turns the five-pair over-fill into the table answer', () => {
    expect(normalizePurpose('10 songs for a workout', { ...heavy, limit: 10 })).toEqual({
      limit: 10,
      energyMin: 70,
      arousalMin: 60,
    });
    expect(normalizePurpose('something for the gym', heavy)).toEqual({
      energyMin: 70,
      arousalMin: 60,
    });
    expect(normalizePurpose('party playlist', heavy)).toEqual({ danceabilityMin: 70 });
    expect(normalizePurpose('music for sleeping', heavy)).toEqual({ energyMax: 90 });
  });

  it('keeps an explicit tempo, in every language the corpus uses', () => {
    const cases: [string, Partial<SearchArgs>][] = [
      ['fast techno for a workout', { genre: 'techno' }],
      ['rápido para correr', {}],
      ['rapide pour courir', {}],
      ['schnell für das Training', {}],
      ['速いジョギング', {}],
    ];
    for (const [sentence, extra] of cases) {
      expect(normalizePurpose(sentence, { ...heavy, ...extra }).bpmMin, sentence).toBe(130);
    }
  });

  it('drops the tempo a purpose sentence did not state, in every language', () => {
    const cases = [
      'música para estudiar',
      'de la musique pour lire',
      'Musik zum Lernen',
      '勉強のための音楽',
      'workout',
    ];
    for (const sentence of cases) {
      const out = normalizePurpose(sentence, heavy);
      expect(out.bpmMin, sentence).toBeUndefined();
      expect(out.bpmMax, sentence).toBeUndefined();
    }
  });

  it('keeps an explicit valence and an explicit danceability', () => {
    expect(
      normalizePurpose('música alegre para bailar', { ...heavy, mood: ['happy'] }),
    ).toEqual({
      mood: ['happy'],
      valenceMin: 60,
      valenceMax: 80,
      danceabilityMin: 70,
      danceabilityMax: 90,
    });
    expect(normalizePurpose('happy workout', { ...heavy, mood: ['happy'] })).toEqual({
      mood: ['happy'],
      valenceMin: 60,
      valenceMax: 80,
      energyMin: 70,
      arousalMin: 60,
    });
  });

  it('leaves a sentence with no purpose word alone', () => {
    for (const sentence of ['sad and slow', 'música triste', 'fast aggressive techno']) {
      expect(normalizePurpose(sentence, heavy)).toEqual(heavy);
    }
  });

  it('keeps a sort the sentence asked for and drops one it did not', () => {
    expect(normalizePurpose('workout sorted by bpm', heavy).sort).toBe('bpm');
    expect(normalizePurpose('workout', heavy).sort).toBeUndefined();
  });

  it('never turns an Intent that was all invention into an empty one', () => {
    const invented: SearchArgs = { sort: 'bpm', bpmMax: 95 };
    expect(normalizePurpose('workout', invented)).toEqual(invented);
  });

  it('leaves duration alone: it is the track’s length, not the purpose’s', () => {
    const out = normalizePurpose('workout', { ...heavy, durationMax: 180 });
    expect(out.durationMax).toBe(180);
  });
});
