/** The numbers the instruction states, and the library's aggregates behind them. */

import type { AxisProfile, LibraryProfile } from '../../domain/snapshot';

import { DURATION_RANGE } from './contract';

/**
 * Every quantity the prompt states and the purpose table prescribes: the library's aggregates,
 * or the device's static numbers when there is no profile.
 */
export interface PromptFacts {
  tracks: number | null;
  genres: readonly string[] | null;
  keys: readonly string[] | null;
  moods: readonly string[] | null;
  bpm: { min: number; max: number; slow: number; fast: number };
  energy: { min: number; max: number; chill: number; workout: number };
  danceability: { min: number; max: number; party: number };
  valence: { min: number; max: number; sad: number; happy: number };
  arousal: { min: number; max: number; workout: number; aggressive: number };
  duration: { min: number; max: number; short: number; long: number };
}

const DEFAULT_FACTS: PromptFacts = {
  tracks: null,
  genres: null,
  keys: null,
  moods: null,
  bpm: { min: 70, max: 180, slow: 95, fast: 125 },
  energy: { min: 0, max: 100, chill: 40, workout: 70 },
  danceability: { min: 0, max: 100, party: 70 },
  valence: { min: 0, max: 100, sad: 40, happy: 60 },
  arousal: { min: 0, max: 100, workout: 60, aggressive: 65 },
  duration: { min: DURATION_RANGE.min, max: DURATION_RANGE.max, short: 180, long: 480 },
};

function roundedAxis(axis: AxisProfile | null, fallback: { min: number; max: number }) {
  return axis ? { min: Math.floor(axis.min), max: Math.ceil(axis.max) } : fallback;
}

/**
 * The prompt's numbers: the library's own when there is a profile, the device's otherwise.
 *
 * Exported for the evaluation harness: the corpus's expected numbers are the same quantities the
 * prompt states (`bpm.slow`, `energy.workout`, …), so the scorer has to read them from here rather
 * than hard-code a second copy that could drift from the prompt.
 */
export function factsOf(profile: LibraryProfile | null): PromptFacts {
  if (!profile) return DEFAULT_FACTS;
  const bpm = roundedAxis(profile.bpm, { min: 70, max: 180 });
  const energy = roundedAxis(profile.energy, { min: 0, max: 100 });
  const danceability = roundedAxis(profile.danceability, { min: 0, max: 100 });
  const valence = roundedAxis(profile.valence, { min: 0, max: 100 });
  const arousal = roundedAxis(profile.arousal, { min: 0, max: 100 });
  const duration = roundedAxis(profile.duration, { min: 0, max: 86_400 });
  return {
    tracks: profile.tracks,
    genres: profile.genres.length > 0 ? profile.genres : null,
    keys: profile.keys.length > 0 ? profile.keys : null,
    moods: profile.moods.length > 0 ? profile.moods : null,
    bpm: {
      ...bpm,
      slow: profile.bpm ? Math.round(profile.bpm.p25) : 95,
      fast: profile.bpm ? Math.round(profile.bpm.p75) : 125,
    },
    energy: {
      ...energy,
      chill: profile.energy ? Math.round(profile.energy.p25) : 40,
      workout: profile.energy ? Math.round(profile.energy.p75) : 70,
    },
    danceability: {
      ...danceability,
      party: profile.danceability ? Math.round(profile.danceability.p75) : 70,
    },
    valence: {
      ...valence,
      sad: profile.valence ? Math.round(profile.valence.p25) : 40,
      happy: profile.valence ? Math.round(profile.valence.p75) : 60,
    },
    arousal: {
      ...arousal,
      workout: profile.arousal ? Math.round(profile.arousal.p75) : 60,
      /* Aggressive and workout share the library's top arousal quartile; the mood aggressive is
       * what tells them apart. The static prompt keeps the 65/60 offset. */
      aggressive: profile.arousal ? Math.round(profile.arousal.p75) : 65,
    },
    duration: {
      ...duration,
      short: profile.duration ? Math.round(profile.duration.p25) : 180,
      long: profile.duration ? Math.round(profile.duration.p75) : 480,
    },
  };
}
