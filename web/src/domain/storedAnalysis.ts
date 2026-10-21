import { MOOD_TAGS, type Analysis, type MoodTag } from './analysis';
import type { ParsedKey } from './keyTag';

/** The analysis as written into the file, minus file identity and provenance. */
export interface StoredAnalysis {
  version: 1;
  bpm: number;
  key: ParsedKey;
  moodTags: Record<MoodTag, number> | null;
  moodLabel: string | null;
  valence: number | null;
  arousal: number | null;
  energy: number | null;
}

const finite = (value: unknown): value is number =>
  typeof value === 'number' && Number.isFinite(value);

const clamp01 = (value: number) => Math.max(0, Math.min(1, value));

function parseMoodTags(value: unknown): Record<MoodTag, number> | null {
  if (!value || typeof value !== 'object') return null;
  const source = value as Record<string, unknown>;
  const tags = {} as Record<MoodTag, number>;
  for (const tag of MOOD_TAGS) {
    const score = source[tag];
    if (!finite(score)) return null;
    tags[tag] = clamp01(score);
  }
  return tags;
}

function parseKey(value: unknown): ParsedKey | null {
  if (!value || typeof value !== 'object') return null;
  const key = value as Record<string, unknown>;
  const { tonic, mode, camelot } = key;
  if (typeof tonic !== 'string' || tonic.length === 0 || tonic.length > 3) return null;
  if (mode !== 'major' && mode !== 'minor') return null;
  if (typeof camelot !== 'string' || !/^\d{1,2}[AB]$/.test(camelot)) return null;
  return { tonic, mode, camelot };
}

export function toStoredAnalysis(analysis: Analysis): StoredAnalysis {
  return {
    version: 1,
    bpm: analysis.tempo.bpm,
    key: {
      tonic: analysis.key.tonic,
      mode: analysis.key.mode,
      camelot: analysis.key.camelot,
    },
    moodTags: analysis.mood.tags,
    moodLabel: analysis.mood.label,
    valence: analysis.mood.valence,
    arousal: analysis.mood.arousal,
    energy: analysis.energy,
  };
}

export function parseStoredAnalysis(raw: string): StoredAnalysis | null {
  let value: unknown;
  try {
    value = JSON.parse(raw);
  } catch {
    return null;
  }
  if (!value || typeof value !== 'object') return null;
  const stored = value as Record<string, unknown>;

  if (stored.version !== 1) return null;
  if (!finite(stored.bpm) || stored.bpm <= 0) return null;
  const key = parseKey(stored.key);
  if (!key) return null;

  const moodTags = stored.moodTags === null ? null : parseMoodTags(stored.moodTags);
  const moodLabel =
    typeof stored.moodLabel === 'string' && stored.moodLabel.trim()
      ? stored.moodLabel.trim()
      : null;
  if (!moodTags && !moodLabel) return null;

  const valence = stored.valence === null ? null : finite(stored.valence) ? clamp01(stored.valence) : null;
  const arousal = stored.arousal === null ? null : finite(stored.arousal) ? clamp01(stored.arousal) : null;
  const energy = stored.energy === null ? null : finite(stored.energy) ? clamp01(stored.energy) : null;

  return {
    version: 1,
    bpm: stored.bpm,
    key,
    moodTags,
    moodLabel,
    valence,
    arousal,
    energy,
  };
}

/** "Am", "F#m", "C" — the compact spelling DJ tools write. */
export function compactKey(tonic: string, mode: 'major' | 'minor'): string {
  return `${tonic}${mode === 'minor' ? 'm' : ''}`;
}

export function dominantMood(tags: Record<MoodTag, number> | null): string | null {
  if (!tags) return null;
  let best: MoodTag = MOOD_TAGS[0];
  for (const tag of MOOD_TAGS) {
    if (tags[tag] > tags[best]) best = tag;
  }
  return best.charAt(0).toUpperCase() + best.slice(1);
}

function number(value: number): string {
  return value.toFixed(2).replace(/\.?0+$/, '');
}

/**
 * Standard tags for other players plus the lossless XTAPE manifest. The
 * standard tags are lossy for mood (a label instead of six probabilities), so
 * the manifest is what this app reads back.
 */
export function analysisTags(analysis: Analysis): Record<string, string> {
  const tags: Record<string, string> = {
    BPM: number(analysis.tempo.bpm),
    INITIALKEY: compactKey(analysis.key.tonic, analysis.key.mode),
  };

  const mood = analysis.mood.label ?? dominantMood(analysis.mood.tags);
  if (mood) tags.MOOD = mood;
  if (analysis.energy !== null) tags.ENERGY = number(analysis.energy);
  tags.XTAPE = JSON.stringify(toStoredAnalysis(analysis));

  return tags;
}
