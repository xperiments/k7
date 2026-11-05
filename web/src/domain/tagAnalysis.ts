import type { TrackMetadata } from '../audio/metadata';
import { ANALYSIS_SCHEMA, type Analysis, type TrackFormat } from './analysis';
import { ANALYZER_VERSION } from './version';

/**
 * A track the tags already answer for.
 *
 * The analyser's expensive work is tempo and key, and a library tagged by Mixed In Key or by
 * a DJ tool already has both. Measuring them again is minutes of inference for a value that
 * was written down years ago — so a row can be built from the tags instead, with the fields
 * the tags cannot carry left unknown (the format has a marker for that, and the player shows
 * "not measured" rather than a zero).
 *
 * The decision is per track and per field: `needsMeasurement` is true as soon as a tag for
 * BPM or key is missing, and the whole analysis then runs so the missing one is measured
 * rather than guessed.
 */

/** The analyser presents tempo folded into this range; a tag is folded the same way. */
const MIN_BPM = 70;
const MAX_BPM = 180;

function foldBpm(bpm: number): number {
  if (!Number.isFinite(bpm) || bpm <= 0) return 0;
  let value = bpm;
  while (value > MAX_BPM) value /= 2;
  while (value < MIN_BPM) value *= 2;
  return Math.round(value * 100) / 100;
}

export interface TaggedTrack {
  name: string;
  path: string;
  size: number;
  format: TrackFormat;
  hash: string;
}

/** Whether this track has to be measured: `tagsOnly` lets the tags answer when they can. */
export function needsMeasurement(metadata: TrackMetadata, tagsOnly: boolean): boolean {
  if (!tagsOnly) return true;
  return metadata.bpm === undefined || metadata.key === undefined;
}

export function tagAnalysis(track: TaggedTrack, metadata: TrackMetadata): Analysis {
  const bpm = metadata.bpm === undefined ? 0 : foldBpm(metadata.bpm);
  /* XTAPE is the lossless record this App writes: it carries the model's mood probabilities
   * and DEAM outputs, which the standard tags cannot. When it is there, the quick path keeps
   * those values instead of presenting the file as only half-known. */
  const stored = metadata.stored;
  const moodTags = stored?.moodTags ?? null;
  const moodLabel = stored?.moodLabel ?? metadata.mood ?? null;
  const energy = stored?.energy ?? metadata.energy ?? null;
  return {
    schema: ANALYSIS_SCHEMA,
    track: { ...track, duration: 0 },
    tempo: {
      bpm,
      rawBpm: bpm,
      confidence: null,
      engine: null,
      escalated: false,
      suggestEscalation: false,
      source: 'metadata',
    },
    key: {
      tonic: metadata.key?.tonic ?? '',
      mode: metadata.key?.mode ?? 'major',
      camelot: metadata.key?.camelot ?? '',
      strength: null,
      source: 'metadata',
    },
    mood: {
      tags: moodTags,
      label: moodLabel,
      // No mood tag and no XTAPE means no mood, not a measured one: the row's flag stays
      // clear and the player reads "not measured".
      source: moodTags !== null || moodLabel !== null ? 'metadata' : 'measured',
      valence: stored?.valence ?? null,
      arousal: stored?.arousal ?? null,
    },
    energy,
    energySource: energy === null ? null : 'metadata',
    provenance: {
      analyzer: `tags ${ANALYZER_VERSION}`,
      modelSet: '',
      window: null,
      analysedAt: new Date().toISOString(),
    },
  };
}
