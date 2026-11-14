/**
 * The assistant's half of **Ask**: a sentence in, an **Intent** out, and nothing else.
 *
 * ADR 0050 settled the shape and ADR 0053 adopted it: the model produces the
 * `library_search` filters, never the results, because a wrong filter is visible and
 * correctable while a confident paragraph about music that does not exist is not. The
 * library's per-track data is never described to the model: the request carries the sentence,
 * the shape instructions, and — when this browser built the image the player is running — the
 * library's aggregates, never a track, an artist, a path or a per-track measurement.
 *
 * `parseIntent` is the boundary: whatever the model says is untrusted text, so every field
 * is checked, unknown keys are dropped, and every number is clamped to the range the
 * device documents (limits from `library_search` in `components/portable/mcp/mcp.c`).
 *
 * `requestIntent` is the only function that asks the model, and it is not tested: the
 * built-in model is Chrome's and CI has none. The schema it passes and the parser it trusts
 * are both tested (ADR 0084).
 *
 * What Ask says back is not this module's: the count, the reading, the source and the save
 * question are computed in `chat.ts` from the Intent and the search, so the model never writes a
 * sentence about a track (ADR 0086).
 */

import { askModel, assistantFailure } from '../builtin-model';
import type { SearchArgs } from '../device/mcp';
import type { AxisProfile, LibraryProfile } from '../domain/snapshot';

/** The five mood heads the device knows. Danceability is a property, not a mood. */
export const MOOD_VALUES = ['happy', 'sad', 'aggressive', 'relaxed', 'party'] as const;

/** `library_search`'s `sort`, exactly. */
export const SORT_VALUES = ['path', 'title', 'bpm', 'energy', 'danceability'] as const;

/** The folded tempo range the Snapshot stores; a BPM bound outside it can never match. */
export const BPM_RANGE = { min: 70, max: 180 } as const;

/** A manual BPM bound, brought into the range the library can answer. */
export function clampBpm(value: number): number {
  return Math.min(BPM_RANGE.max, Math.max(BPM_RANGE.min, value));
}

const CATEGORY_QUERIES = new Set([
  'song',
  'songs',
  'music',
  'track',
  'tracks',
  'tune',
  'tunes',
  'something',
  'anything',
  'stuff',
]);

/**
 * A bare category is not a name: as a text search "songs" can only narrow the result to paths
 * that contain the word. "party songs" is not bare and is left alone.
 */
export function isCategoryQuery(query: string | undefined): boolean {
  if (!query) return false;
  const normalized = query
    .trim()
    .toLowerCase()
    .replace(/^the\s+/, '')
    .replace(/[.!?,]+$/, '')
    .trim();
  return CATEGORY_QUERIES.has(normalized);
}

const NAME_MARKERS = new Set(['by', 'from', 'called', 'named', 'titled', 'artist', 'band', 'album']);
const CONTEXT_MARKERS = new Set([
  'for',
  'to',
  'during',
  'while',
  'with',
  'without',
  'at',
  'in',
  'on',
  'about',
  'like',
]);
const DETERMINERS = new Set([
  'a',
  'an',
  'the',
  'my',
  'our',
  'your',
  'some',
  'this',
  'that',
  'these',
  'those',
]);

/** A word without its punctuation, lowercased: "Songs," -> "songs". */
function plainWord(value: string): string {
  return value.toLowerCase().replace(/[^\p{L}\p{N}#]/gu, '');
}

/**
 * Whether the sentence marks the query as a name. A name marker before it wins ("by Daft
 * Punk"); a purpose or context marker means it is not a name ("for a workout"); with no marker
 * at all, capitalization decides. A bare category is never a name.
 */
export function isNameQuery(sentence: string, query: string): boolean {
  if (isCategoryQuery(query)) return false;
  const needle = query.trim();
  if (!needle) return false;

  const at = sentence.toLowerCase().indexOf(needle.toLowerCase());
  const capital = /\p{Lu}/u.test(needle);
  if (at < 0) return capital;

  const before = sentence.slice(0, at).trim().split(/\s+/).filter(Boolean);
  let index = before.length - 1;
  while (index >= 0 && DETERMINERS.has(plainWord(before[index]))) index -= 1;
  const marker = index >= 0 ? plainWord(before[index]) : '';

  if (NAME_MARKERS.has(marker)) return true;
  if (CONTEXT_MARKERS.has(marker)) return false;
  return capital;
}

/** A failed Intent: one sentence the Ask area can show as it is. */
export interface IntentFailure {
  error: string;
  /** The answer was not a usable object at all, so one more ask can help. */
  retryable?: boolean;
}

function failure(error: string, retryable = false): IntentFailure {
  return retryable ? { error, retryable } : { error };
}

/* -------------------------------------------------------------------------- */
/* Validation                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * A key said two ways: "B minor", "B min" and "Bm" are one; "B flat minor" and "B♭ minor" are
 * "Bbm"; "8A" is its own. Used only to notice a query that restates a key, so the mapping is
 * deliberately small and never rewrites what is sent to the device.
 */
function keyWords(value: string): string {
  return value
    .toLowerCase()
    .replace(/\s+/g, '')
    .replace(/♭/g, 'b')
    .replace(/♯/g, '#')
    .replace(/flat/g, 'b')
    .replace(/sharp/g, '#')
    .replace(/minor$/, 'm')
    .replace(/min$/, 'm')
    .replace(/major$/, '')
    .replace(/maj$/, '');
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** The device's byte ceilings: `char query[256]` and `char genre[64]`, both NUL-terminated. */
const QUERY_BYTES = 255;
const GENRE_BYTES = 63;

/** A UTF-8 prefix of at most `maxBytes`, never cutting a code point in half. */
function truncateBytes(value: string, maxBytes: number): string {
  const encoded = new TextEncoder().encode(value);
  if (encoded.length <= maxBytes) return value;
  let end = maxBytes;
  while (end > 0 && (encoded[end] & 0xc0) === 0x80) end -= 1;
  return new TextDecoder().decode(encoded.subarray(0, end));
}

/**
 * A non-empty trimmed string, or undefined. The cap is the device's, in bytes: a string over
 * it makes `library_search` refuse the whole search, not just that filter.
 */
function text(value: unknown, maxBytes: number): string | undefined {
  if (typeof value !== 'string') return undefined;
  const trimmed = value.trim();
  return trimmed ? truncateBytes(trimmed, maxBytes) : undefined;
}

/** A finite number clamped into `[min, max]`; anything else is not a number at all. */
function numeric(value: unknown, min: number, max: number, round = false): number | undefined {
  if (typeof value !== 'number' || !Number.isFinite(value)) return undefined;
  const clamped = Math.min(max, Math.max(min, value));
  return round ? Math.round(clamped) : clamped;
}

/**
 * One bound pair, clamped and ordered. A model can answer "140 to 120" or "90 to 20"; the
 * library searches `min <= max`, so an inverted pair is swapped rather than made impossible.
 */
function orderedPair(
  min: unknown,
  max: unknown,
  low: number,
  high: number,
  round = false,
): [number | undefined, number | undefined] {
  const a = numeric(min, low, high, round);
  const b = numeric(max, low, high, round);
  return a !== undefined && b !== undefined && a > b ? [b, a] : [a, b];
}

/** An array of strings, trimmed, without empties or repeats. */
function stringList(value: unknown): string[] {
  if (!Array.isArray(value)) return [];
  const out: string[] = [];
  for (const item of value) {
    if (typeof item !== 'string') continue;
    const trimmed = item.trim();
    if (trimmed && !out.includes(trimmed)) out.push(trimmed);
  }
  return out;
}

function isMood(value: string): value is (typeof MOOD_VALUES)[number] {
  return (MOOD_VALUES as readonly string[]).includes(value.toLowerCase());
}

function isSort(value: string): value is (typeof SORT_VALUES)[number] {
  return (SORT_VALUES as readonly string[]).includes(value.toLowerCase());
}

type Unwrapped =
  | { ok: true; value: Record<string, unknown> }
  | { ok: false; error: string };

/** The first `{…}` in the text, for the model that wraps its JSON in a sentence. */
function braced(text: string): string {
  const start = text.indexOf('{');
  const end = text.lastIndexOf('}');
  return start >= 0 && end > start ? text.slice(start, end + 1) : '';
}

/**
 * The answer as one object. Code fences are tolerated (a `response_format` is a request,
 * not a promise), prose around the object is tolerated, and anything that is not an object
 * is an error rather than an empty Intent — silence would look like "no filters".
 */
function unwrap(answer: string): Unwrapped {
  const trimmed = answer.trim();
  if (!trimmed) return { ok: false, error: 'The model answered nothing.' };

  const unfenced = trimmed
    .replace(/^```(?:json)?\s*/i, '')
    .replace(/\s*```$/, '')
    .trim();

  for (const candidate of [unfenced, braced(unfenced)]) {
    if (!candidate) continue;
    let parsed: unknown;
    try {
      parsed = JSON.parse(candidate);
    } catch {
      continue;
    }
    if (!isRecord(parsed)) {
      return { ok: false, error: 'The answer was not a JSON object of filters.' };
    }
    return { ok: true, value: parsed };
  }

  return { ok: false, error: 'The answer was not JSON, so there is nothing to search for.' };
}

/* -------------------------------------------------------------------------- */
/* The Intent                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * The model's answer as `library_search` arguments.
 *
 * The model is asked for snake_case keys named after the tool's own arguments; the app
 * hands `searchTracks` camelCase ones. Unknown keys are dropped rather than passed on, and
 * a number outside the range the Snapshot stores is clamped rather than discarded: "140" and
 * "1400" BPM both come back runnable, in the folded tempo the library has, and the visible
 * Intent shows what happened.
 */
export function parseIntent(answer: string): SearchArgs | IntentFailure {
  const parsed = unwrap(answer);
  if (!parsed.ok) return failure(parsed.error, true);
  const raw = parsed.value;

  const out: SearchArgs = {};

  const query = text(raw.query, QUERY_BYTES);
  if (query) out.query = query;
  const genre = text(raw.genre, GENRE_BYTES);
  if (genre) out.genre = genre;

  // BPM: the Snapshot stores folded tempo only, 70..180, so a bound outside it can never match.
  // `arg_bpm` would accept 0..400 and compare that against folded values; clamping here keeps
  // the Intent to what the library has.
  const [bpmMin, bpmMax] = orderedPair(raw.bpm_min, raw.bpm_max, BPM_RANGE.min, BPM_RANGE.max);
  if (bpmMin !== undefined) out.bpmMin = bpmMin;
  if (bpmMax !== undefined) out.bpmMax = bpmMax;

  // The four regressions are percentages on the wire (0..100 integers).
  const [energyMin, energyMax] = orderedPair(raw.energy_min, raw.energy_max, 0, 100, true);
  if (energyMin !== undefined) out.energyMin = energyMin;
  if (energyMax !== undefined) out.energyMax = energyMax;
  const [danceabilityMin, danceabilityMax] = orderedPair(
    raw.danceability_min,
    raw.danceability_max,
    0,
    100,
    true,
  );
  if (danceabilityMin !== undefined) out.danceabilityMin = danceabilityMin;
  if (danceabilityMax !== undefined) out.danceabilityMax = danceabilityMax;
  const [valenceMin, valenceMax] = orderedPair(raw.valence_min, raw.valence_max, 0, 100, true);
  if (valenceMin !== undefined) out.valenceMin = valenceMin;
  if (valenceMax !== undefined) out.valenceMax = valenceMax;
  const [arousalMin, arousalMax] = orderedPair(raw.arousal_min, raw.arousal_max, 0, 100, true);
  if (arousalMin !== undefined) out.arousalMin = arousalMin;
  if (arousalMax !== undefined) out.arousalMax = arousalMax;

  // Keys are kept as the model wrote them: the device understands Camelot codes and note
  // names, and deciding which one a string is would be guessing on its behalf.
  const keys = stringList(raw.key).slice(0, 24);
  if (keys.length > 0) {
    out.key = keys;
    // The model sometimes says a key twice, once as a key and once as words to find. Filters
    // are ANDed, so "B minor" beside key "Bm" can only narrow the result to nothing.
    if (query && keys.some((key) => keyWords(query) === keyWords(key))) delete out.query;
  }

  const moods = stringList(raw.mood).filter(isMood).map((mood) => mood.toLowerCase());
  if (moods.length > 0) out.mood = Array.from(new Set(moods)).slice(0, MOOD_VALUES.length);

  if (typeof raw.sort === 'string' && isSort(raw.sort)) out.sort = raw.sort.toLowerCase() as
    (typeof SORT_VALUES)[number];

  const offset = numeric(raw.offset, 0, 199, true);
  const limit = numeric(raw.limit, 1, 100, true);
  if (offset !== undefined || limit !== undefined) {
    const start = offset ?? 0;
    // The device refuses `offset + limit > 200`; trim the page instead of losing the ask.
    out.limit = Math.max(1, Math.min(limit ?? 200 - start, 200 - start));
    if (offset !== undefined) out.offset = start;
  }

  if (Object.keys(out).length === 0) {
    return failure('The answer had no filters this library understands, so nothing can be run.');
  }
  return out;
}

const NUMERIC_FILTERS = [
  'bpmMin',
  'bpmMax',
  'energyMin',
  'energyMax',
  'danceabilityMin',
  'danceabilityMax',
  'valenceMin',
  'valenceMax',
  'arousalMin',
  'arousalMax',
] as const;

/**
 * The same Intent without its numeric ranges, when a mood is left to search on. Ask uses this
 * once when an exact search returns nothing: a mood alone can still find something, and the
 * visible Intent then shows the filters that actually ran.
 */
export function relaxRanges(intent: SearchArgs): SearchArgs | null {
  if (!intent.mood || intent.mood.length === 0) return null;
  if (!NUMERIC_FILTERS.some((key) => intent[key] !== undefined)) return null;
  const relaxed: SearchArgs = { ...intent };
  for (const key of NUMERIC_FILTERS) delete relaxed[key];
  return relaxed;
}

/* -------------------------------------------------------------------------- */
/* The ask                                                                     */
/* -------------------------------------------------------------------------- */

/**
 * The instruction that turns a sentence into filters. It names the tool's arguments, their
 * ranges and their closed sets, and nothing else: there is no track in it, and there is no
 * room in it for one.
 */
interface PromptFacts {
  tracks: number | null;
  genres: readonly string[] | null;
  keys: readonly string[] | null;
  moods: readonly string[] | null;
  bpm: { min: number; max: number; slow: number; fast: number };
  energy: { min: number; max: number; chill: number; workout: number };
  danceability: { min: number; max: number; party: number };
  valence: { min: number; max: number; sad: number; happy: number };
  arousal: { min: number; max: number; workout: number; aggressive: number };
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
};

function roundedAxis(axis: AxisProfile | null, fallback: { min: number; max: number }) {
  return axis ? { min: Math.floor(axis.min), max: Math.ceil(axis.max) } : fallback;
}

/** The prompt's numbers: the library's own when there is a profile, the device's otherwise. */
function factsOf(profile: LibraryProfile | null): PromptFacts {
  if (!profile) return DEFAULT_FACTS;
  const bpm = roundedAxis(profile.bpm, { min: 70, max: 180 });
  const energy = roundedAxis(profile.energy, { min: 0, max: 100 });
  const danceability = roundedAxis(profile.danceability, { min: 0, max: 100 });
  const valence = roundedAxis(profile.valence, { min: 0, max: 100 });
  const arousal = roundedAxis(profile.arousal, { min: 0, max: 100 });
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
      aggressive: profile.arousal ? Math.round(profile.arousal.p75) : 65,
    },
  };
}

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
    '  key      ["8A"] or ["Am"]. Never also put a key in "query".',
    `  mood     any of: ${f.moods ? f.moods.join(', ') : 'happy, sad, aggressive, relaxed, party'}`,
    '  sort     one of: path, title, bpm, energy, danceability (ascending only)',
    '  limit    1..100        offset    0..199',
    '',
    'RULES',
    '- Every filter narrows the result, so use as few as the sentence justifies.',
    '- One-sided words set one bound: "fast" = bpm_min only, "slow" = bpm_max only,',
    '  "not too loud" = energy_max only. Keep min <= max.',
    `- Clamp tempos to ${f.bpm.min}..${f.bpm.max}; "very slow" is bpm_max ${f.bpm.slow}, not the floor.`,
    '- Use mood for feelings and the numeric keys only for stated qualities. Do not add',
    '  both for the same word unless the table below does.',
    '- A count of tracks is "limit". A duration is limit = 15 per hour, max 100.',
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
  ['play something', '{}'],
];
/**
 * The few-shot turns for one library: the same lessons with the profile's own numbers, so an
 * example never teaches a BPM or an energy the schema would refuse.
 */
export function intentExamples(profile: LibraryProfile | null = null): [string, string][] {
  if (!profile) return INTENT_EXAMPLES;
  const f = factsOf(profile);
  const genre = profile.genres[0];
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
    ['play something', '{}'],
  ];
}


/**
 * The Intent's shape, passed as `responseConstraint`. It states the same contract `parseIntent`
 * enforces: closed keys, the range the Snapshot stores, and the five moods and five sorts. The
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
 * moods it has, and each axis's own min/max. The default is the device's range; the narrowed
 * one is still a request, and `parseIntent` remains the judge.
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
  ] as const;
  for (const [minKey, maxKey, axis] of axes) {
    properties[minKey] = {
      ...(INTENT_SCHEMA.properties[minKey] as object),
      minimum: axis.min,
      maximum: axis.max,
    };
    properties[maxKey] = {
      ...(INTENT_SCHEMA.properties[maxKey] as object),
      minimum: axis.min,
      maximum: axis.max,
    };
  }
  return { ...INTENT_SCHEMA, properties };
}

/**
 * Ask the built-in model for an Intent.
 *
 * Every failure is a sentence the area can show as it is, and none of them is fatal: the filter
 * form stays usable, which is the whole point of the Intent being visible. One repair follows an
 * answer that was not a usable object — an empty Intent is the model's correct answer to a
 * sentence with no musical constraint, and asking twice would only repeat it — and one follows
 * a query that is not a name the sentence used, because the sentence says which name it meant.
 */
export async function requestIntent(
  sentence: string,
  profile: LibraryProfile | null = null,
): Promise<SearchArgs | IntentFailure> {
  const asked = sentence.trim();
  if (!asked) return failure('Say what you are looking for first.');

  const system = buildSystemPrompt(profile);
  const schema = intentSchema(profile);
  const examples = intentExamples(profile);

  let answer: string;
  try {
    answer = await askModel({ system, schema, sentence: asked, examples });
  } catch (error) {
    return failure(assistantFailure(error));
  }

  const intent = parseIntent(answer);

  if (!('error' in intent)) {
    const query = intent.query;
    if (!query || isNameQuery(asked, query)) return intent;
    return repairQuery(asked, answer, query, system, schema, examples);
  }

  if (!intent.retryable) return intent;

  let repaired: string;
  try {
    repaired = await askModel({
      system,
      schema,
      sentence: `${asked}\n\nYour previous answer was: ${answer}\nIt failed because: ${intent.error}\nAnswer again with one JSON object and nothing else.`,
      examples,
    });
  } catch (error) {
    return failure(assistantFailure(error));
  }
  return parseIntent(repaired);
}

/**
 * The model sometimes answers a name with a word the sentence did not mark as one ("songs" for
 * "songs by Daft Punk", "workout" for "10 songs for a workout"). The parser cannot know the
 * name, but this function has the sentence: ask once more with the specific failure. If the
 * word comes back and is still not a name, drop it so it cannot narrow the search.
 */
async function repairQuery(
  asked: string,
  answer: string,
  query: string,
  system: string,
  schema: object,
  examples: [string, string][],
): Promise<SearchArgs | IntentFailure> {
  let repaired: string;
  try {
    repaired = await askModel({
      system,
      schema,
      sentence: `${asked}\n\nYour previous answer was: ${answer}\nIts "query" is "${query}", which is not a name the sentence used. If the sentence names an artist, an album or a title, put that name in "query"; otherwise leave "query" out. Answer again with one JSON object.`,
      examples,
    });
  } catch (error) {
    return failure(assistantFailure(error));
  }

  const fixed = parseIntent(repaired);
  if ('error' in fixed) return fixed;
  if (!fixed.query || isNameQuery(asked, fixed.query)) return fixed;

  delete fixed.query;
  if (Object.keys(fixed).length === 0) {
    return failure(
      'The model answered with a word that is not a name the sentence used and nothing else, so there are no filters to run.',
    );
  }
  return fixed;
}
