/**
 * The sentence's own numbers: a count, a listening session, a track's length, a tempo, an offset,
 * a key. The model is weakest at exactly these, and they are the part a person can check in the
 * Intent; so a pure extractor reads them before the model's answer is trusted.
 *
 * It is applied only to an Intent that already parsed: a sentence that is not a search ("add 5
 * songs to the playlist") must stay a refusal. A field it finds replaces the model's field; a
 * pair is one field, so a stated maximum clears an invented minimum. A field it does not find
 * keeps the model's value.
 *
 * It never writes a name, a mood or a genre: those are the fuzzy semantics the model is for.
 * A bare "around 140" is left to the model; without "bpm" or "tempo" it can be a count.
 */

import type { SearchArgs } from '../../device/mcp';
import type { LibraryProfile } from '../../domain/snapshot';
import { keyCode } from '../../domain/key';

import { clampBpm, DURATION_RANGE } from './contract';
import { factsOf, type PromptFacts } from './facts';
import { normalizeKeys } from './keys';

const LIMIT_PER_HOUR = 15;
const MAX_LIMIT = 100;
const MAX_OFFSET = 199;
const AROUND_BPM = 5;

/* -------------------------------------------------------------------------- */
/* Numbers                                                                     */
/* -------------------------------------------------------------------------- */

const ONES = [
  'one',
  'two',
  'three',
  'four',
  'five',
  'six',
  'seven',
  'eight',
  'nine',
  'ten',
  'eleven',
  'twelve',
  'thirteen',
  'fourteen',
  'fifteen',
  'sixteen',
  'seventeen',
  'eighteen',
  'nineteen',
] as const;

const TENS = [
  'twenty',
  'thirty',
  'forty',
  'fifty',
  'sixty',
  'seventy',
  'eighty',
  'ninety',
] as const;

/** A tens word with an optional one-word tail: "twenty five", "twenty-five", "twenty". */
const COMPOUND = `(?:${TENS.join('|')})(?:[\\s-](?:${ONES.slice(0, 9).join('|')}))?`;
const NUMBER = `(?:\\d{1,3}|${COMPOUND}|${ONES.join('|')}|hundred)`;

function parseNumber(token: string): number | undefined {
  const value = token.trim().toLowerCase().replace(/-/g, ' ');
  if (/^\d+$/.test(value)) return Number(value);
  if (value === 'a' || value === 'an') return 1;
  if (value === 'hundred') return 100;

  const parts = value.split(/\s+/);
  if (parts.length === 2) {
    const tens = TENS.indexOf(parts[0] as (typeof TENS)[number]);
    const ones = ONES.indexOf(parts[1] as (typeof ONES)[number]);
    if (tens >= 0 && ones >= 0 && ones < 9) return (tens + 2) * 10 + ones + 1;
  }

  const one = ONES.indexOf(value as (typeof ONES)[number]);
  if (one >= 0) return one + 1;
  const ten = TENS.indexOf(value as (typeof TENS)[number]);
  if (ten >= 0) return (ten + 2) * 10;
  return undefined;
}

function limitOf(value: number): number {
  return Math.max(1, Math.min(MAX_LIMIT, Math.round(value)));
}

function offsetOf(value: number): number {
  return Math.max(0, Math.min(MAX_OFFSET, Math.round(value)));
}

/* -------------------------------------------------------------------------- */
/* Counts                                                                      */
/* -------------------------------------------------------------------------- */

/** The words allowed between a count and its noun, so "the 20 most danceable tracks" works. */
const COUNT_MODIFIERS =
  '(?:most|least|new|best|latest|popular|great|favourite|favorite|danceable|recent|old|top)';

const COUNT = new RegExp(
  `\\b(${NUMBER})\\s+(?:${COUNT_MODIFIERS}\\s+){0,2}(?:songs?|tracks?|tunes?)\\b`,
  'i',
);

const HOURS = new RegExp(`\\b(${NUMBER}|an?)\\s*(?:hours?|hrs?)\\b`, 'i');
const SKIP = new RegExp(`\\bskip\\s+(${NUMBER})\\b`, 'i');

/** A stated number of tracks, not a listening session and not a BPM. */
function countOf(sentence: string): number | undefined {
  const match = COUNT.exec(sentence);
  if (!match) return undefined;
  const value = parseNumber(match[1]);
  return value === undefined ? undefined : limitOf(value);
}

/** A listening session is a count at fifteen tracks an hour, capped at a page. */
function sessionOf(sentence: string): number | undefined {
  const match = HOURS.exec(sentence);
  if (!match) return undefined;
  const hours = parseNumber(match[1]);
  return hours === undefined ? undefined : limitOf(hours * LIMIT_PER_HOUR);
}

function skipOf(sentence: string): number | undefined {
  const match = SKIP.exec(sentence);
  if (!match) return undefined;
  const value = parseNumber(match[1]);
  return value === undefined ? undefined : offsetOf(value);
}

/* -------------------------------------------------------------------------- */
/* Genres                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * The genre words a sentence can name without a profile. A profile's own genres win; this list is
 * deliberately conservative where a word is commonly something else ("house" in "house party",
 * "lo-fi" in a textual style, "dance" as a mood), so the sentence does not invent a genre.
 */
const DEFAULT_GENRES = [
  'ambient', 'techno', 'reggaeton', 'jazz', 'classical', 'hip-hop', 'drum and bass', 'trance',
  'funk', 'soul', 'disco', 'rock', 'pop', 'metal', 'blues', 'country', 'folk', 'reggae',
  'synthwave', 'electro', 'punk', 'indie', 'grunge', 'salsa', 'cumbia', 'bachata', 'afrobeat',
  'amapiano', 'drill', 'trap', 'dubstep', 'edm', 'r&b', 'k-pop', 'j-pop',
] as const;

function genreOf(sentence: string, profile: LibraryProfile | null): string | undefined {
  const genres = profile?.genres?.length ? profile.genres : DEFAULT_GENRES;
  for (const genre of genres) {
    /* A whole word, in the sentence's own case: "the album Discovery" is not disco, and the
     * artist "Daft Punk" is not the punk genre. A sentence that says "techno" or "ambient" is. */
    const escaped = genre.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const pattern = new RegExp(`(?:^|[^\\p{L}\\p{N}])${escaped}(?:$|[^\\p{L}\\p{N}])`, 'u');
    if (pattern.test(sentence)) return genre;
  }
  return undefined;
}

/* -------------------------------------------------------------------------- */
/* Durations                                                                   */
/* -------------------------------------------------------------------------- */

const DURATION = new RegExp(
  `\\b(under|below|less than|up to|at most|max(?:imum)?|over|above|more than|at least|min(?:imum)?)\\s+(${NUMBER})\\s*(seconds?|secs?|minutes?|mins?)\\b`,
  'i',
);

const DURATION_MAX = /^(?:under|below|less than|up to|at most|max)/;

/** A long or a short track, rather than a listening session: the prompt's "long mixes". */
const LONG_TRACK = /^(?:long|longer|extended)\b/;
const SHORT_TRACK = /^(?:short|shorter|brief)\b/;

function durationOf(
  sentence: string,
  facts: PromptFacts,
): Pick<SearchArgs, 'durationMin' | 'durationMax'> | undefined {
  if (LONG_TRACK.test(sentence)) return { durationMin: facts.duration.long };
  if (SHORT_TRACK.test(sentence)) return { durationMax: facts.duration.short };
  const match = DURATION.exec(sentence);
  if (!match) return undefined;
  const value = parseNumber(match[2]);
  if (value === undefined) return undefined;
  const unit = /^sec/.test(match[3]) ? 1 : 60;
  const seconds = Math.max(
    DURATION_RANGE.min,
    Math.min(DURATION_RANGE.max, Math.round(value * unit)),
  );
  return DURATION_MAX.test(match[1]) ? { durationMax: seconds } : { durationMin: seconds };
}

/* -------------------------------------------------------------------------- */
/* BPM                                                                         */
/* -------------------------------------------------------------------------- */

const BPM = '(?:bpm|beats per minute|tempo)';

const BPM_PAIRS = [
  new RegExp(
    `\\b(?:between|from)\\s+(${NUMBER})\\s*(?:${BPM})?\\s+(?:and|to|-)\\s+(${NUMBER})\\s*${BPM}\\b`,
    'i',
  ),
  new RegExp(`\\b(${NUMBER})\\s*(?:${BPM})?\\s*(?:-|to)\\s*(${NUMBER})\\s*${BPM}\\b`, 'i'),
];
const BPM_UNDER = new RegExp(
  `\\b(?:under|below|less than|up to|at most|max(?:imum)?)\\s+(${NUMBER})\\s*${BPM}\\b`,
  'i',
);
const BPM_OVER = new RegExp(
  `\\b(?:over|above|more than|at least|min(?:imum)?)\\s+(${NUMBER})\\s*${BPM}\\b`,
  'i',
);
const BPM_BARE = new RegExp(
  `(\\baround\\b|\\babout\\b|\\bapproximately\\b|~)?\\s*(${NUMBER})\\s*${BPM}\\b`,
  'i',
);
const BPM_PREFIX = new RegExp(`\\b${BPM}\\s*(?:of|at|:)?\\s*(${NUMBER})\\b`, 'i');

function bpmOf(sentence: string): Pick<SearchArgs, 'bpmMin' | 'bpmMax'> | undefined {
  for (const pattern of BPM_PAIRS) {
    const match = pattern.exec(sentence);
    if (!match) continue;
    const first = parseNumber(match[1]);
    const second = parseNumber(match[2]);
    if (first === undefined || second === undefined) continue;
    const low = clampBpm(Math.min(first, second));
    const high = clampBpm(Math.max(first, second));
    return { bpmMin: low, bpmMax: high };
  }

  const under = BPM_UNDER.exec(sentence);
  const over = BPM_OVER.exec(sentence);
  const max = under ? parseNumber(under[1]) : undefined;
  const min = over ? parseNumber(over[1]) : undefined;
  if (min !== undefined || max !== undefined) {
    const low = min !== undefined ? clampBpm(min) : undefined;
    const high = max !== undefined ? clampBpm(max) : undefined;
    if (low !== undefined && high !== undefined && low > high) {
      return { bpmMin: high, bpmMax: low };
    }
    return {
      ...(low !== undefined ? { bpmMin: low } : {}),
      ...(high !== undefined ? { bpmMax: high } : {}),
    };
  }

  const bare = BPM_BARE.exec(sentence);
  if (bare) {
    const value = parseNumber(bare[2]);
    if (value !== undefined) {
      if (bare[1] !== undefined) {
        return { bpmMin: clampBpm(value - AROUND_BPM), bpmMax: clampBpm(value + AROUND_BPM) };
      }
      return { bpmMin: clampBpm(value), bpmMax: clampBpm(value) };
    }
  }

  const prefix = BPM_PREFIX.exec(sentence);
  if (prefix) {
    const value = parseNumber(prefix[1]);
    if (value !== undefined) return { bpmMin: clampBpm(value), bpmMax: clampBpm(value) };
  }
  return undefined;
}

/* -------------------------------------------------------------------------- */
/* Keys                                                                        */
/* -------------------------------------------------------------------------- */

const CAMELOT = /\b(1[0-2]|[1-9])([AB])\b/gi;
const NOTE_WORD = new RegExp(
  `\\b([A-G])(?:\\s*(#|b|♯|♭|sharp|flat))?\\s*(minor|min|menor|mineur|moll|マイナー|短調|major|maj|mayor|majeur|dur|メジャー|長調)(?=[\\s.,;:!?]|$)`,
  'gi',
);
const NOTE_SUFFIX = new RegExp(
  `\\b([A-G])(#|b|♯|♭)?(m|mi|min|mino|ma|maj|majo)(?=[\\s.,;:!?]|$)`,
  'gi',
);

/** A note+mode is a key only at the start or after a marker; a lowercase article is not a key. */
function acceptedNote(sentence: string, match: RegExpExecArray): boolean {
  const note = match[1];
  if (note !== note.toUpperCase()) return false;
  const before = sentence.slice(0, match.index ?? 0);
  if (/^\s*$/.test(before)) return true;
  const previous = before.trim().split(/\s+/).pop()?.toLowerCase().replace(/[^\p{L}]+/gu, '');
  return previous === 'in' || previous === 'key' || previous === 'camelot' || previous === 'of'
    || previous === 'is';
}

function keysOf(sentence: string): string[] {
  const found: string[] = [];
  const seen = new Set<number>();

  const add = (candidate: string, accepted: boolean): void => {
    if (!accepted) return;
    const normalized = normalizeKeys(sentence, { key: [candidate] }).key ?? [];
    for (const key of normalized) {
      const code = keyCode(key);
      if (code === 0 || seen.has(code)) continue;
      seen.add(code);
      found.push(key);
    }
  };

  for (const match of sentence.matchAll(CAMELOT)) {
    add(`${match[1]}${match[2].toUpperCase()}`, true);
  }
  for (const match of sentence.matchAll(NOTE_WORD)) {
    add(match[0], acceptedNote(sentence, match));
  }
  for (const match of sentence.matchAll(NOTE_SUFFIX)) {
    add(match[0], acceptedNote(sentence, match));
  }
  return found.slice(0, 24);
}

/* -------------------------------------------------------------------------- */
/* The extractor and its application                                           */
/* -------------------------------------------------------------------------- */

/** The fields the sentence states. A field absent here keeps the model's value. */
export function sentenceFilters(
  sentence: string,
  profile: LibraryProfile | null = null,
): Partial<SearchArgs> {
  const text = sentence.toLowerCase().replace(/[–—]/g, '-');
  const facts = factsOf(profile);
  const out: Partial<SearchArgs> = {};

  const limit = countOf(text) ?? sessionOf(text);
  if (limit !== undefined) out.limit = limit;
  const offset = skipOf(text);
  if (offset !== undefined) out.offset = offset;

  const genre = genreOf(sentence, profile);
  if (genre) out.genre = genre;
  const duration = durationOf(text, facts);
  if (duration) Object.assign(out, duration);
  const bpm = bpmOf(text);
  if (bpm) Object.assign(out, bpm);

  const keys = keysOf(sentence);
  if (keys.length > 0) out.key = keys;
  return out;
}

/**
 * Whether two key lists name the same keys. A different spelling of one key ("A minor" and "Am")
 * is not a conflict; the model's spelling survives when the sentence adds nothing.
 */
function sameKeys(left: string[] | undefined, right: string[] | undefined): boolean {
  const a = (left ?? []).map(keyCode).filter((code) => code !== 0);
  const b = (right ?? []).map(keyCode).filter((code) => code !== 0);
  if (a.length === 0 || b.length === 0) return false;
  return a.length === b.length && a.every((code) => b.includes(code));
}

/**
 * Apply the sentence's fields over the model's Intent. A pair is one field: a stated maximum
 * clears the model's minimum, and a stated minimum clears its maximum. A count from the sentence
 * replaces the model's limit; the offset and the device's 200-row page are kept together. A key
 * the model already has, under another spelling, is left alone.
 */
export function applySentenceFilters(
  intent: SearchArgs,
  filters: Partial<SearchArgs>,
): SearchArgs {
  const out: SearchArgs = { ...intent };

  if (filters.bpmMin !== undefined || filters.bpmMax !== undefined) {
    delete out.bpmMin;
    delete out.bpmMax;
    if (filters.bpmMin !== undefined) out.bpmMin = filters.bpmMin;
    if (filters.bpmMax !== undefined) out.bpmMax = filters.bpmMax;
  }

  if (filters.genre !== undefined) out.genre = filters.genre;
  if (filters.durationMin !== undefined || filters.durationMax !== undefined) {
    delete out.durationMin;
    delete out.durationMax;
    if (filters.durationMin !== undefined) out.durationMin = filters.durationMin;
    if (filters.durationMax !== undefined) out.durationMax = filters.durationMax;
  }

  if (filters.key && filters.key.length > 0 && !sameKeys(out.key, filters.key)) {
    out.key = filters.key;
  }

  if (filters.limit !== undefined || filters.offset !== undefined) {
    const start = filters.offset ?? out.offset ?? 0;
    const limit = filters.limit ?? out.limit;
    if (limit !== undefined) out.limit = Math.max(1, Math.min(limit, 200 - start));
    if (filters.offset !== undefined) out.offset = start;
  }

  return out;
}
