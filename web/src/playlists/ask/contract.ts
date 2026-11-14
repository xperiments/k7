/**
 * The Intent's contract: the ranges `library_search` documents, the closed sets, the device's
 * byte ceilings, the six measurement axes, and the failure a caller can show.
 *
 * The numbers come from `library_search` in `components/portable/mcp/mcp.c` and from the
 * Snapshot format; a value outside them can never match. `parseIntent` is the judge, but this
 * module is where the bounds are written down.
 */

/** The five mood heads the device knows. Danceability is a property, not a mood. */
export const MOOD_VALUES = ['happy', 'sad', 'aggressive', 'relaxed', 'party'] as const;

/** `library_search`'s `sort`, exactly. */
export const SORT_VALUES = ['path', 'title', 'bpm', 'energy', 'danceability', 'duration'] as const;

/** The folded tempo range the Snapshot stores; a BPM bound outside it can never match. */
export const BPM_RANGE = { min: 70, max: 180 } as const;

/** A track's length is seconds in the Intent; a day is beyond any track a card can hold. */
export const DURATION_RANGE = { min: 0, max: 86_400 } as const;

/** A manual BPM bound, brought into the range the library can answer. */
export function clampBpm(value: number): number {
  return Math.min(BPM_RANGE.max, Math.max(BPM_RANGE.min, value));
}

/** A failed Intent: one sentence the Ask area can show as it is. */
export interface IntentFailure {
  error: string;
  /** The answer was not a usable object at all, so one more ask can help. */
  retryable?: boolean;
}

export function failure(error: string, retryable = false): IntentFailure {
  return retryable ? { error, retryable } : { error };
}

/** The device's byte ceilings: `char query[256]` and `char genre[64]`, both NUL-terminated. */
export const QUERY_BYTES = 255;
export const GENRE_BYTES = 63;

/** The six measurement axes: a pair is one range, and three ranges is the over-fill threshold. */
export const NUMERIC_AXES = [
  ['bpmMin', 'bpmMax'],
  ['energyMin', 'energyMax'],
  ['danceabilityMin', 'danceabilityMax'],
  ['valenceMin', 'valenceMax'],
  ['arousalMin', 'arousalMax'],
  ['durationMin', 'durationMax'],
] as const;

export const NUMERIC_FILTERS = NUMERIC_AXES.flat();
