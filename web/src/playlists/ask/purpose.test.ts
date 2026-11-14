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
    expect(normalizePurpose('music for sleeping', heavy)).toEqual({ energyMax: 40 });
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

  it('fills an Intent that was all invention with the table', () => {
    const invented: SearchArgs = { sort: 'bpm', bpmMax: 95 };
    expect(normalizePurpose('workout', invented)).toEqual({
      energyMin: 70,
      arousalMin: 60,
    });
  });

  it('fills a limit-only answer and keeps the count', () => {
    expect(normalizePurpose('10 songs for a workout', { limit: 10 })).toEqual({
      limit: 10,
      energyMin: 70,
      arousalMin: 60,
    });
  });

  it('fills an empty answer with the table', () => {
    expect(normalizePurpose('party playlist', {})).toEqual({
      mood: ['party'],
      danceabilityMin: 70,
    });
  });

  it('keeps a mood the model gave and adds the missing measurement', () => {
    expect(normalizePurpose('música alegre para bailar', { mood: ['happy'] })).toEqual({
      mood: ['happy'],
      danceabilityMin: 70,
    });
  });

  it('snaps a prescribed bound on an explicit axis, and keeps the other side', () => {
    expect(
      normalizePurpose('música alegre para bailar', {
        mood: ['happy'],
        valenceMin: 60,
        valenceMax: 80,
        danceabilityMin: 50,
        danceabilityMax: 90,
      }),
    ).toEqual({
      mood: ['happy'],
      valenceMin: 60,
      valenceMax: 80,
      danceabilityMin: 70,
      danceabilityMax: 90,
    });
  });

  it('takes the table values from the profile, not the static facts', () => {
    expect(
      normalizePurpose('10 songs for a workout', { energyMin: 60, energyMax: 90 }, PROFILE),
    ).toEqual({ energyMin: 74 });
    expect(normalizePurpose('10 songs for a workout', { limit: 10 }, PROFILE)).toEqual({
      limit: 10,
      energyMin: 74,
      arousalMin: 73,
    });
  });

  it('leaves duration alone: it is the track’s length, not the purpose’s', () => {
    const out = normalizePurpose('workout', { ...heavy, durationMax: 180 });
    expect(out.durationMax).toBe(180);
  });
});

describe("the library's moods", () => {
  const PARTY = { ...PROFILE, moods: ['happy', 'party', 'relaxed'] };

  it('leaves out a prescribed mood the profile does not have', () => {
    expect(purposeFilters('party playlist', PROFILE)).toEqual({ danceabilityMin: 72 });
    expect(purposeFilters('party playlist', PARTY)).toEqual({
      mood: ['party'],
      danceabilityMin: 72,
    });
    expect(purposeFilters('de la musique calme pour lire', PROFILE)).toEqual({
      mood: ['relaxed'],
      energyMax: 40,
    });
    expect(
      purposeFilters('de la musique calme pour lire', { ...PROFILE, moods: ['happy', 'party'] }),
    ).toEqual({ energyMax: 40 });
    expect(purposeFilters('party playlist', { ...PROFILE, moods: [] })).toEqual({
      danceabilityMin: 72,
    });
  });

  it('keeps the table without a profile', () => {
    expect(purposeFilters('party playlist', null)).toEqual({
      mood: ['party'],
      danceabilityMin: 70,
    });
  });

  it('drops the model’s mood when the library does not have it', () => {
    const intent: SearchArgs = { mood: ['party'], danceabilityMin: 70, danceabilityMax: 90 };
    expect(normalizePurpose('party playlist', intent, PROFILE)).toEqual({
      danceabilityMin: 72,
    });
    expect(normalizePurpose('party playlist', intent, PARTY)).toEqual({
      mood: ['party'],
      danceabilityMin: 72,
    });
  });

  it('fills the numeric side when the model only gives the unavailable mood', () => {
    expect(normalizePurpose('party playlist', { mood: ['party'] }, PROFILE)).toEqual({
      danceabilityMin: 72,
    });
  });

  it('keeps S11’s happy mood when the library has it', () => {
    const intent: SearchArgs = {
      mood: ['happy'],
      valenceMin: 60,
      valenceMax: 80,
      danceabilityMin: 70,
      danceabilityMax: 90,
    };
    expect(normalizePurpose('música alegre para bailar', intent, PROFILE)).toEqual({
      mood: ['happy'],
      valenceMin: 60,
      valenceMax: 80,
      danceabilityMin: 72,
      danceabilityMax: 90,
    });
  });
});
