/**
 * The instruction, the few-shot turns and the schema: everything one model call is sent.
 *
 * The schema is a request and `parseIntent` is the judge. The prompt states the same contract
 * the parser enforces, and a profile narrows both to the library's own numbers.
 */

import type { LibraryProfile } from '../../domain/snapshot';

import { BPM_RANGE, DURATION_RANGE, MOOD_VALUES, SORT_VALUES } from './contract';
import { factsOf } from './facts';

/**
 * The instruction, with the library's real ranges when a profile is available. Without one it
 * is the static prompt: the device's ranges and no claim about a library.
 */
export function buildSystemPrompt(profile: LibraryProfile | null): string {
  const f = factsOf(profile);
  const lines = [
    'You turn ONE sentence from a listener into search filters for their personal music',
    'library. You cannot see the library. Never invent a track, artist, album or genre, and',
    'never answer with a list of music. Output one JSON object only.',
    '',
    'KEYS (all optional; omit what the sentence does not ask for):',
    '  query    words that appear in a title, artist, album or path. Only a name the',
    '           sentence actually contains. Never a category word (songs, music, tracks,',
    '           something, stuff).',
    profile
      ? '  genre    one genre, only if the sentence names one and it is in this library.'
      : '  genre    one genre, only if the sentence names one.',
    `  bpm_min, bpm_max           ${f.bpm.min}..${f.bpm.max}`,
    `  energy_min/max             ${f.energy.min}..${f.energy.max}`,
    `  danceability_min/max       ${f.danceability.min}..${f.danceability.max}`,
    `  valence_min/max            ${f.valence.min}..${f.valence.max}`,
    `  arousal_min/max            ${f.arousal.min}..${f.arousal.max}`,
    `  duration_min/max           ${f.duration.min}..${f.duration.max} (seconds)`,
    '  key      ["8A"] or ["Am"]. Never also put a key in "query".',
    `  mood     any of: ${f.moods ? f.moods.join(', ') : 'happy, sad, aggressive, relaxed, party'}`,
    '  sort     one of: path, title, bpm, energy, danceability, duration (ascending only)',
    '  limit    1..100        offset    0..199',
    '',
    'RULES',
    '- Every filter narrows the result, so use as few as the sentence justifies.',
    '- One-sided words set one bound: "fast" = bpm_min only, "slow" = bpm_max only,',
    '  "not too loud" = energy_max only. Keep min <= max.',
    `- Clamp tempos to ${f.bpm.min}..${f.bpm.max}; "very slow" is bpm_max ${f.bpm.slow}, not the floor.`,
    '- Use mood for feelings and the numeric keys only for stated qualities. Do not add',
    '  both for the same word unless the table below does.',
    '- A count of tracks is "limit". A listening session ("two hours of ...") is limit = 15',
    '  per hour, max 100. A length of the tracks themselves ("under 3 minutes", "long mixes")',
    '  is duration_max / duration_min, in seconds.',
    '- "next page" or "skip N" is offset. Otherwise omit offset.',
    '- "Similar to X", "like X" or anything you cannot filter: put the name X in "query"',
    '  if given, otherwise {}.',
    '- No musical constraint at all: {}.',
    '- The sentence may be in any language. Keys and values stay in English; names in',
    '  "query" stay as written.',
    '',
    'WORD -> FILTER',
    `  chill, calm, sleep, study  -> mood ["relaxed"], energy_max ${f.energy.chill}`,
    `  workout, energetic, intense -> energy_min ${f.energy.workout}, arousal_min ${f.arousal.workout}`,
    `  party, dance               -> mood ["party"], danceability_min ${f.danceability.party}`,
    `  sad, melancholic           -> mood ["sad"], valence_max ${f.valence.sad}`,
    `  happy, upbeat, sunny       -> mood ["happy"], valence_min ${f.valence.happy}`,
    `  fast / slow                -> bpm_min ${f.bpm.fast} / bpm_max ${f.bpm.slow}`,
    `  short / long               -> duration_max ${f.duration.short} / duration_min ${f.duration.long}`,
    `  aggressive, heavy          -> mood ["aggressive"], arousal_min ${f.arousal.aggressive}`,
  ];
  if (profile) {
    lines.push(
      '',
      'THIS LIBRARY',
      `- ${profile.tracks} tracks; a limit above that is the whole library.`,
    );
    if (f.genres) {
      lines.push(
        `- Genres present (most common first): ${f.genres.join(', ')}. Use one of these or omit genre.`,
      );
    }
    if (f.keys) lines.push(`- Keys present (Camelot): ${f.keys.join(', ')}.`);
    if (f.moods) {
      lines.push(`- Moods present: ${f.moods.join(', ')}. Use one of these or omit mood.`);
    }
  }
  return lines.join('\n');
}

/**
 * Few-shot turns: a sentence and the answer it should produce. They are passed as
 * `initialPrompts` after the system message, so the model copies the shape instead of reading
 * about it. Every answer is a valid Intent; the empty object is the designed no-filter answer.
 */
export const INTENT_EXAMPLES: [string, string][] = [
  ['twenty relaxed tracks for sleeping', '{"mood":["relaxed"],"energy_max":35,"limit":20}'],
  ['two hours of fast techno', '{"genre":"techno","bpm_min":125,"limit":30}'],
  ['songs by Daft Punk', '{"query":"Daft Punk"}'],
  ['something in B minor, danceable', '{"key":["Bm"],"danceability_min":65}'],
  ['música tranquila para estudiar', '{"mood":["relaxed"],"energy_max":40}'],
  ['slowest tracks first', '{"sort":"bpm"}'],
  ['tracks under three minutes', '{"duration_max":180}'],
  ['long mixes', '{"duration_min":480}'],
  ['play something', '{}'],
  ['music like Bonobo', '{"query":"Bonobo"}'],
  ['happy techno', '{"genre":"techno","mood":["happy"]}'],
  ['next page', '{"limit":20,"offset":20}'],
  ['canciones de bonobo', '{"query":"bonobo"}'],
  ['a workout party', '{"energy_min":70,"arousal_min":60}'],
];
/**
 * The few-shot turns for one library: the same lessons with the profile's own numbers, so an
 * example never teaches a BPM or an energy the schema would refuse.
 */
export function intentExamples(profile: LibraryProfile | null = null): [string, string][] {
  if (!profile) return INTENT_EXAMPLES;
  const f = factsOf(profile);
  /* Use the second genre when there is one, so the most common genre is not the only one the
   * turns teach. The choice is deterministic for a library, so the measured prompt is stable. */
  const genre = profile.genres.length > 1 ? profile.genres[1] : profile.genres[0];
  const mood = profile.moods[0] ?? 'happy';
  return [
    [
      'twenty relaxed tracks for sleeping',
      JSON.stringify({ mood: ['relaxed'], energy_max: f.energy.chill, limit: 20 }),
    ],
    genre
      ? [`two hours of fast ${genre}`, JSON.stringify({ genre, bpm_min: f.bpm.fast, limit: 30 })]
      : ['two hours of fast music', JSON.stringify({ bpm_min: f.bpm.fast, limit: 30 })],
    ['songs by Daft Punk', '{"query":"Daft Punk"}'],
    [
      'something in B minor, danceable',
      JSON.stringify({ key: ['Bm'], danceability_min: f.danceability.party }),
    ],
    [
      'música tranquila para estudiar',
      JSON.stringify({ mood: ['relaxed'], energy_max: f.energy.chill }),
    ],
    ['slowest tracks first', '{"sort":"bpm"}'],
    ['tracks under three minutes', JSON.stringify({ duration_max: f.duration.short })],
    ['long mixes', JSON.stringify({ duration_min: f.duration.long })],
    ['play something', '{}'],
    ['music like Bonobo', '{"query":"Bonobo"}'],
    genre
      ? [
          `${mood} ${genre}`,
          JSON.stringify({ genre, mood: [mood] }),
        ]
      : [`${mood} music`, JSON.stringify({ mood: [mood] })],
    ['next page', '{"limit":20,"offset":20}'],
    ['canciones de bonobo', '{"query":"bonobo"}'],
    [
      'a workout party',
      JSON.stringify({ energy_min: f.energy.workout, arousal_min: f.arousal.workout }),
    ],
  ];
}

/**
 * The Intent's shape, passed as `responseConstraint`. It states the same contract `parseIntent`
 * enforces: closed keys, the range the Snapshot stores, and the closed moods and sorts. The
 * schema is a request and the parser is the judge — a model that ignores a range is still
 * clamped — but with the constraint Chrome returns this shape, and the spike showed it accepts
 * ranges, enums and `additionalProperties` (ADR 0084).
 */
export const INTENT_SCHEMA = {
  type: 'object',
  additionalProperties: false,
  required: [],
  properties: {
    bpm_min: { type: 'number', minimum: BPM_RANGE.min, maximum: BPM_RANGE.max },
    bpm_max: { type: 'number', minimum: BPM_RANGE.min, maximum: BPM_RANGE.max },
    energy_min: { type: 'integer', minimum: 0, maximum: 100 },
    energy_max: { type: 'integer', minimum: 0, maximum: 100 },
    danceability_min: { type: 'integer', minimum: 0, maximum: 100 },
    danceability_max: { type: 'integer', minimum: 0, maximum: 100 },
    valence_min: { type: 'integer', minimum: 0, maximum: 100 },
    valence_max: { type: 'integer', minimum: 0, maximum: 100 },
    arousal_min: { type: 'integer', minimum: 0, maximum: 100 },
    arousal_max: { type: 'integer', minimum: 0, maximum: 100 },
    duration_min: { type: 'number', minimum: DURATION_RANGE.min, maximum: DURATION_RANGE.max },
    duration_max: { type: 'number', minimum: DURATION_RANGE.min, maximum: DURATION_RANGE.max },
    key: { type: 'array', items: { type: 'string' } },
    mood: { type: 'array', items: { type: 'string', enum: MOOD_VALUES } },
    sort: { type: 'string', enum: SORT_VALUES },
    offset: { type: 'integer', minimum: 0, maximum: 199 },
    limit: { type: 'integer', minimum: 1, maximum: 100 },
    genre: { type: 'string' },
    query: { type: 'string' },
  },
} as const;

/**
 * The schema, narrowed to the library's actual values when there is a profile: the genres and
 * moods it has, each axis's ceiling, and each axis's floor except a track's length, whose floor
 * stays the device's 0 so a sentence below the library's shortest track is still representable.
 * The key stays unconstrained: the grammar is the judge. The default is the device's range; the
 * narrowed one is still a request, and `parseIntent` remains the judge.
 */
export function intentSchema(profile: LibraryProfile | null = null) {
  if (!profile) return INTENT_SCHEMA;
  const f = factsOf(profile);
  const properties: Record<string, object> = { ...INTENT_SCHEMA.properties };
  if (profile.genres.length > 0) properties.genre = { type: 'string', enum: profile.genres };
  if (profile.moods.length > 0) {
    properties.mood = { type: 'array', items: { type: 'string', enum: profile.moods } };
  }
  const axes = [
    ['bpm_min', 'bpm_max', f.bpm],
    ['energy_min', 'energy_max', f.energy],
    ['danceability_min', 'danceability_max', f.danceability],
    ['valence_min', 'valence_max', f.valence],
    ['arousal_min', 'arousal_max', f.arousal],
    ['duration_min', 'duration_max', f.duration],
  ] as const;
  for (const [minKey, maxKey, axis] of axes) {
    /* A library can have no track under 45 s; the sentence can still ask for one, and the search
     * answers empty rather than the schema refusing the request. The other floors keep the
     * library's value — a known narrowness, reported rather than silently changed. */
    const minimum = minKey === 'duration_min' ? DURATION_RANGE.min : axis.min;
    properties[minKey] = {
      ...(INTENT_SCHEMA.properties[minKey] as object),
      minimum,
      maximum: axis.max,
    };
    properties[maxKey] = {
      ...(INTENT_SCHEMA.properties[maxKey] as object),
      minimum,
      maximum: axis.max,
    };
  }
  return { ...INTENT_SCHEMA, properties };
}
