/**
 * The boundary: whatever the model says is untrusted text, so every field is checked, unknown
 * keys are dropped, and every number is clamped to the range the device documents.
 *
 * The parser can only judge the answer in front of it. Whether a `query` is a name the sentence
 * used is a question about the sentence, so `isNameQuery` lives here and `request.ts` asks it.
 */

import type { SearchArgs } from '../../device/mcp';

import {
  BPM_RANGE,
  DURATION_RANGE,
  failure,
  GENRE_BYTES,
  MOOD_VALUES,
  QUERY_BYTES,
  SORT_VALUES,
  type IntentFailure,
} from './contract';

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

const NAME_MARKERS = new Set([
  'by',
  'from',
  'called',
  'named',
  'titled',
  'artist',
  'band',
  'album',
  /* "like X" and "similar to X" name a thing to sound like: the prompt says to put X in
   * "query", so the repair has to agree. */
  'like',
]);
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
  /* "similar to" is two words; the marker is "to", and the word before it is the signal. */
  if (marker === 'to' && index > 0 && plainWord(before[index - 1]) === 'similar') return true;
  if (CONTEXT_MARKERS.has(marker)) return false;
  return capital;
}

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

  // A track's length, in seconds. 0 is "no bound" to `library_search`, so a bound of 0 is
  // dropped rather than ordered against the other one (and never shown as "length from 0:00").
  let durationMin = numeric(raw.duration_min, DURATION_RANGE.min, DURATION_RANGE.max);
  let durationMax = numeric(raw.duration_max, DURATION_RANGE.min, DURATION_RANGE.max);
  if (durationMin === 0) durationMin = undefined;
  if (durationMax === 0) durationMax = undefined;
  if (durationMin !== undefined && durationMax !== undefined && durationMin > durationMax) {
    [durationMin, durationMax] = [durationMax, durationMin];
  }
  if (durationMin) out.durationMin = durationMin;
  if (durationMax) out.durationMax = durationMax;

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
