/**
 * The frozen corpus for **Ask**: sixty sentences from the research note, plus six added while
 * porting it, each with the Intent it should produce and the condition that decides whether it
 * did.
 *
 * The note (`docs/research/assistant-evaluation.md` §6) fixes the sentences; this file is the
 * contract. `expected` is what `parseIntent` should yield for the *static* prompt
 * (`profile: null`), in the App's camelCase, with the prompt's own thresholds. `derived` names
 * the thresholds that move with a library profile, so the scorer can ask `factsOf(profile)` for
 * them instead of inventing a second set of numbers.
 *
 * `must` is the row's pass condition, not a prose wish: `outcome` says whether a runnable Intent
 * or a refusal is correct, and the rest is a machine-checkable predicate. A row passes only when
 * its automatic verdict is identical in all three runs (spec §3).
 *
 * Provenance of every `expected`: the note's table §6 and the WORD→FILTER table inside
 * `buildSystemPrompt` — not a model. The six rows marked `added` cover gaps the note names but
 * does not test; every other defect fixed while porting is listed in the spec's "Corpus fixes".
 *
 * This module is data and pure helpers only: it imports `SearchArgs` for its type and nothing at
 * runtime.
 */

import type { SearchArgs } from '../device/mcp';

/** A numeric filter key, camelCase as `SearchArgs` spells it. */
export type NumericKey =
  | 'bpmMin'
  | 'bpmMax'
  | 'durationMin'
  | 'durationMax'
  | 'energyMin'
  | 'energyMax'
  | 'danceabilityMin'
  | 'danceabilityMax'
  | 'valenceMin'
  | 'valenceMax'
  | 'arousalMin'
  | 'arousalMax';

/** A number in the prompt's WORD→FILTER table, named by the quantity it is. */
export type FactKey =
  | 'bpm.slow'
  | 'bpm.fast'
  | 'energy.chill'
  | 'energy.workout'
  | 'danceability.party'
  | 'valence.sad'
  | 'valence.happy'
  | 'arousal.workout'
  | 'arousal.aggressive'
  | 'duration.short'
  | 'duration.long';

/** The pass condition: what the row's answer has to be, and what it must not be. */
export interface Must {
  /**
   * `parse` needs a runnable Intent, `fail` needs a refusal, and `either` accepts both — for a
   * sentence where the product's own answer is a judgement call ("music that isn't sad").
   */
  outcome: 'parse' | 'fail' | 'either';
  /** Filter keys that must be present. A pair is two keys; each is checked. */
  required?: readonly string[];
  /** Filter keys that must be absent. */
  forbidden?: readonly string[];
  /**
   * The most measurement axes the Intent may set (bpm, duration, energy, danceability, valence,
   * arousal). A min/max pair is one axis; `limit` and `offset` are not axes.
   */
  maxPairs?: number;
  /** Absolute tolerance per key: a number is ±, a pair is an inclusive interval. */
  tolerances?: Readonly<Record<string, number | readonly [number, number]>>;
  /** Exact values: numbers compare exactly, strings case-folded, arrays as sets. */
  exact?: Readonly<Record<string, unknown>>;
  /** The query must be one of these, case-folded — for a sentence naming two artists. */
  queryOneOf?: readonly string[];
  /** The row only passes if the reference library returns a track. Needs scope 3. */
  rows?: 'nonEmpty';
  note?: string;
}

export interface CorpusRow {
  /** `S1`–`S12` are the spike's; `13`–`60` the note's corpus; `N1`–`N6` the added rows. */
  id: string;
  set: 'spike' | 'corpus';
  sentence: string;
  expected: Partial<SearchArgs> | null;
  must: Must;
  /** Numeric expectations the prompt takes from the library rather than from the device. */
  derived?: Readonly<Partial<Record<NumericKey, FactKey>>>;
  /**
   * The Intent is the best the contract can express, and the contract cannot express the
   * sentence. Reported separately: it is not a model failure.
   */
  contractGap?: boolean;
  /** One of the six rows added while porting the note's sixty. */
  added?: true;
}

/** Every measurement filter, for the rows that must carry no range at all. */
const NO_RANGES = [
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
  'durationMin',
  'durationMax',
] as const;

/**
 * The sixty rows of §6, S1–S12 verbatim as the regression subset, with the porting fixes the
 * spec lists (17, 18, 57, 58, S8's contract gap, and the value-level bans of 22, 25 and 56).
 */
export const CORPUS: readonly CorpusRow[] = [
  /* ---------------------------------------------------------------- S1–S12 */
  {
    id: 'S1',
    set: 'spike',
    sentence: 'tracks between 85 and 95 BPM',
    expected: { bpmMin: 85, bpmMax: 95 },
    must: {
      outcome: 'parse',
      required: ['bpmMin', 'bpmMax'],
      maxPairs: 1,
      tolerances: { bpmMin: 3, bpmMax: 3 },
      rows: 'nonEmpty',
    },
  },
  {
    id: 'S2',
    set: 'spike',
    sentence: 'fast aggressive techno',
    expected: { genre: 'techno', mood: ['aggressive'], bpmMin: 125, arousalMin: 65 },
    must: {
      outcome: 'parse',
      required: ['genre', 'mood', 'bpmMin'],
      maxPairs: 2,
    },
    derived: { bpmMin: 'bpm.fast', arousalMin: 'arousal.aggressive' },
  },
  {
    id: 'S3',
    set: 'spike',
    sentence: 'happy party songs',
    expected: { mood: ['happy', 'party'] },
    must: { outcome: 'parse', required: ['mood'], forbidden: ['query'], maxPairs: 0 },
  },
  {
    id: 'S4',
    set: 'spike',
    sentence: 'sad and slow',
    expected: { mood: ['sad'], valenceMax: 40, bpmMax: 95 },
    must: { outcome: 'parse', required: ['mood'], forbidden: ['bpmMin'], maxPairs: 2 },
    derived: { valenceMax: 'valence.sad', bpmMax: 'bpm.slow' },
  },
  {
    id: 'S5',
    set: 'spike',
    sentence: 'something in A minor',
    expected: { key: ['Am'] },
    must: {
      outcome: 'parse',
      required: ['key'],
      forbidden: ['query'],
      maxPairs: 0,
      exact: { key: ['Am'] },
    },
  },
  {
    id: 'S6',
    set: 'spike',
    sentence: 'Camelot 8A',
    expected: { key: ['8A'] },
    must: {
      outcome: 'parse',
      required: ['key'],
      forbidden: ['query'],
      maxPairs: 0,
      exact: { key: ['8A'] },
    },
  },
  {
    id: 'S7',
    set: 'spike',
    sentence: 'about an hour of relaxed music',
    expected: { mood: ['relaxed'], limit: 15 },
    must: {
      outcome: 'parse',
      required: ['mood', 'limit'],
      forbidden: ['bpmMin', 'bpmMax'],
      tolerances: { limit: [12, 18] },
    },
  },
  {
    id: 'S8',
    set: 'spike',
    sentence: 'the 20 most danceable tracks',
    expected: { sort: 'danceability', limit: 20 },
    must: {
      outcome: 'parse',
      required: ['sort', 'limit'],
      exact: { sort: 'danceability', limit: 20 },
      note: 'the Intent is right and the result is the least danceable twenty: the contract sorts ascending only',
    },
    contractGap: true,
  },
  {
    id: 'S9',
    set: 'spike',
    sentence: 'songs by Daft Punk',
    expected: { query: 'Daft Punk' },
    must: {
      outcome: 'parse',
      required: ['query'],
      forbidden: ['genre', 'mood'],
      exact: { query: 'Daft Punk' },
    },
  },
  {
    id: 'S10',
    set: 'spike',
    sentence: '10 songs for a workout',
    expected: { limit: 10, energyMin: 70, arousalMin: 60 },
    must: {
      outcome: 'parse',
      required: ['limit'],
      forbidden: ['bpmMin', 'bpmMax', 'danceabilityMin', 'danceabilityMax', 'valenceMin', 'valenceMax'],
      maxPairs: 2,
      exact: { limit: 10 },
      rows: 'nonEmpty',
    },
    derived: { energyMin: 'energy.workout', arousalMin: 'arousal.workout' },
  },
  {
    id: 'S11',
    set: 'spike',
    sentence: 'música alegre para bailar',
    expected: { mood: ['happy'], danceabilityMin: 70 },
    must: { outcome: 'parse', required: ['mood'], maxPairs: 1 },
    derived: { danceabilityMin: 'danceability.party' },
  },
  {
    id: 'S12',
    set: 'spike',
    sentence: 'de la musique calme pour lire',
    expected: { mood: ['relaxed'], energyMax: 40 },
    must: { outcome: 'parse', required: ['mood'], maxPairs: 1 },
    derived: { energyMax: 'energy.chill' },
  },

  /* ----------------------------------------------------------------- 13–60 */
  {
    id: '13',
    set: 'corpus',
    sentence: 'tracks under 100 BPM',
    expected: { bpmMax: 100 },
    must: {
      outcome: 'parse',
      required: ['bpmMax'],
      forbidden: ['bpmMin'],
      maxPairs: 1,
      tolerances: { bpmMax: 3 },
      rows: 'nonEmpty',
    },
  },
  {
    id: '14',
    set: 'corpus',
    sentence: 'at least 130 BPM',
    expected: { bpmMin: 130 },
    must: {
      outcome: 'parse',
      required: ['bpmMin'],
      forbidden: ['bpmMax'],
      maxPairs: 1,
      tolerances: { bpmMin: 3 },
    },
  },
  {
    id: '15',
    set: 'corpus',
    sentence: 'slow',
    expected: { bpmMax: 95 },
    must: { outcome: 'parse', required: ['bpmMax'], forbidden: ['bpmMin'], maxPairs: 1 },
    derived: { bpmMax: 'bpm.slow' },
  },
  {
    id: '16',
    set: 'corpus',
    sentence: 'fast',
    expected: { bpmMin: 125 },
    must: { outcome: 'parse', required: ['bpmMin'], forbidden: ['bpmMax'], maxPairs: 1 },
    derived: { bpmMin: 'bpm.fast' },
  },
  {
    id: '17',
    set: 'corpus',
    sentence: 'very slow',
    expected: { bpmMax: 95 },
    must: {
      outcome: 'parse',
      required: ['bpmMax'],
      forbidden: ['bpmMin'],
      maxPairs: 1,
      tolerances: { bpmMax: 5 },
      note: 'the prompt fixes "very slow" at bpm_max 95, not a second bound',
    },
    derived: { bpmMax: 'bpm.slow' },
  },
  {
    id: '18',
    set: 'corpus',
    sentence: '140 to 120 BPM',
    expected: { bpmMin: 120, bpmMax: 140 },
    must: {
      outcome: 'parse',
      required: ['bpmMin', 'bpmMax'],
      maxPairs: 1,
      exact: { bpmMin: 120, bpmMax: 140 },
      note: 'parseIntent swaps the pair, so the assertion is on the parsed result',
    },
  },
  {
    id: '19',
    set: 'corpus',
    sentence: 'techno',
    expected: { genre: 'techno' },
    must: { outcome: 'parse', required: ['genre'], forbidden: ['query'], maxPairs: 0 },
  },
  {
    id: '20',
    set: 'corpus',
    sentence: 'some jazz please',
    expected: { genre: 'jazz' },
    must: { outcome: 'parse', required: ['genre'], forbidden: ['query'], maxPairs: 0 },
  },
  {
    id: '21',
    set: 'corpus',
    sentence: 'reggaeton for the party',
    expected: { genre: 'reggaeton', mood: ['party'], danceabilityMin: 70 },
    must: {
      outcome: 'parse',
      required: ['genre', 'mood', 'danceabilityMin'],
      maxPairs: 1,
    },
    derived: { danceabilityMin: 'danceability.party' },
  },
  {
    id: '22',
    set: 'corpus',
    sentence: 'lo-fi beats to relax to',
    expected: { mood: ['relaxed'], energyMax: 40 },
    must: {
      outcome: 'parse',
      required: ['mood'],
      forbidden: ['query', 'bpmMin', 'bpmMax'],
      maxPairs: 1,
      note: '"lo-fi" is not a name the sentence marked, and the note\'s !{query:"lo-fi"} is expressed as a query ban',
    },
    derived: { energyMax: 'energy.chill' },
  },
  {
    id: '23',
    set: 'corpus',
    sentence: 'the album Discovery',
    expected: { query: 'Discovery' },
    must: {
      outcome: 'parse',
      required: ['query'],
      forbidden: ['genre'],
      exact: { query: 'Discovery' },
    },
  },
  {
    id: '24',
    set: 'corpus',
    sentence: 'play Blue Monday',
    expected: { query: 'Blue Monday' },
    must: {
      outcome: 'parse',
      required: ['query'],
      forbidden: ['genre', 'mood'],
      exact: { query: 'Blue Monday' },
      maxPairs: 0,
    },
  },
  {
    id: '25',
    set: 'corpus',
    sentence: 'songs by the Beatles',
    expected: { query: 'the Beatles' },
    must: {
      outcome: 'parse',
      required: ['query'],
      forbidden: ['genre', 'mood'],
      queryOneOf: ['the Beatles', 'Beatles'],
      note: 'the note\'s "query contains beatles" is a case-folded one-of',
    },
  },
  {
    id: '26',
    set: 'corpus',
    sentence: 'music like Radiohead',
    expected: { query: 'Radiohead' },
    must: {
      outcome: 'parse',
      required: ['query'],
      forbidden: ['genre'],
      exact: { query: 'Radiohead' },
      note: 'the prompt tells the model to put X in query for "like X"; the repair must not drop it',
    },
  },
  {
    id: '27',
    set: 'corpus',
    sentence: 'something like Bonobo but faster',
    expected: { query: 'Bonobo', bpmMin: 125 },
    must: {
      outcome: 'parse',
      required: ['query', 'bpmMin'],
      exact: { query: 'Bonobo' },
      maxPairs: 1,
    },
    derived: { bpmMin: 'bpm.fast' },
  },
  {
    id: '28',
    set: 'corpus',
    sentence: 'a song called Nightcall',
    expected: { query: 'Nightcall' },
    must: {
      outcome: 'parse',
      required: ['query'],
      forbidden: ['mood', 'genre'],
      exact: { query: 'Nightcall' },
    },
  },
  {
    id: '29',
    set: 'corpus',
    sentence: 'something in F# minor',
    expected: { key: ['F#m'] },
    must: {
      outcome: 'parse',
      required: ['key'],
      forbidden: ['query'],
      maxPairs: 0,
      exact: { key: ['F#m'] },
      note: 'keyCode is the device\'s parser: F#m and Fmin are one key, F♯m and "F sharp minor" are not',
    },
  },
  {
    id: '30',
    set: 'corpus',
    sentence: 'B flat major',
    expected: { key: ['Bb'] },
    must: {
      outcome: 'parse',
      required: ['key'],
      forbidden: ['query'],
      maxPairs: 0,
      exact: { key: ['Bb'] },
    },
  },
  {
    id: '31',
    set: 'corpus',
    sentence: 'tracks in 8A or 9A',
    expected: { key: ['8A', '9A'] },
    must: {
      outcome: 'parse',
      required: ['key'],
      forbidden: ['query'],
      maxPairs: 0,
      exact: { key: ['8A', '9A'] },
    },
  },
  {
    id: '32',
    set: 'corpus',
    sentence: 'E minor to dance to',
    expected: { key: ['Em'], danceabilityMin: 70 },
    must: { outcome: 'parse', required: ['key', 'danceabilityMin'], maxPairs: 1 },
    derived: { danceabilityMin: 'danceability.party' },
  },
  {
    id: '33',
    set: 'corpus',
    sentence: 'happy',
    expected: { mood: ['happy'], valenceMin: 60 },
    must: {
      outcome: 'parse',
      required: ['mood'],
      forbidden: ['bpmMin', 'bpmMax'],
      maxPairs: 1,
      tolerances: { valenceMin: 5 },
    },
    derived: { valenceMin: 'valence.happy' },
  },
  {
    id: '34',
    set: 'corpus',
    sentence: 'sad songs',
    expected: { mood: ['sad'], valenceMax: 40 },
    must: {
      outcome: 'parse',
      required: ['mood', 'valenceMax'],
      forbidden: ['query'],
      maxPairs: 1,
    },
    derived: { valenceMax: 'valence.sad' },
  },
  {
    id: '35',
    set: 'corpus',
    sentence: 'relaxing music',
    expected: { mood: ['relaxed'], energyMax: 40 },
    must: { outcome: 'parse', required: ['mood'], forbidden: ['query'], maxPairs: 1 },
    derived: { energyMax: 'energy.chill' },
  },
  {
    id: '36',
    set: 'corpus',
    sentence: 'aggressive',
    expected: { mood: ['aggressive'], arousalMin: 65 },
    must: { outcome: 'parse', required: ['mood'], maxPairs: 1 },
    derived: { arousalMin: 'arousal.aggressive' },
  },
  {
    id: '37',
    set: 'corpus',
    sentence: 'party',
    expected: { mood: ['party'], danceabilityMin: 70 },
    must: { outcome: 'parse', required: ['mood'], maxPairs: 1 },
    derived: { danceabilityMin: 'danceability.party' },
  },
  {
    id: '38',
    set: 'corpus',
    sentence: 'music for sleeping',
    expected: { mood: ['relaxed'], energyMax: 40 },
    must: {
      outcome: 'parse',
      required: ['mood', 'energyMax'],
      forbidden: ['bpmMin', 'bpmMax'],
      maxPairs: 1,
      rows: 'nonEmpty',
    },
    derived: { energyMax: 'energy.chill' },
  },
  {
    id: '39',
    set: 'corpus',
    sentence: 'something to study to',
    expected: { mood: ['relaxed'], energyMax: 40 },
    must: {
      outcome: 'parse',
      required: ['mood'],
      forbidden: [
        'bpmMin',
        'bpmMax',
        'danceabilityMin',
        'danceabilityMax',
        'valenceMin',
        'valenceMax',
      ],
      maxPairs: 1,
    },
    derived: { energyMax: 'energy.chill' },
  },
  {
    id: '40',
    set: 'corpus',
    sentence: 'party playlist',
    expected: { mood: ['party'], danceabilityMin: 70 },
    must: {
      outcome: 'parse',
      required: ['mood'],
      forbidden: ['bpmMin', 'bpmMax'],
      maxPairs: 1,
    },
    derived: { danceabilityMin: 'danceability.party' },
  },
  {
    id: '41',
    set: 'corpus',
    sentence: 'music for a dinner party',
    expected: { mood: ['party'], danceabilityMin: 70 },
    must: {
      outcome: 'parse',
      required: ['mood'],
      forbidden: ['bpmMin', 'bpmMax', 'valenceMin', 'valenceMax'],
      maxPairs: 1,
      rows: 'nonEmpty',
    },
    derived: { danceabilityMin: 'danceability.party' },
  },
  {
    id: '42',
    set: 'corpus',
    sentence: 'workout',
    expected: { energyMin: 70, arousalMin: 60 },
    must: {
      outcome: 'parse',
      required: ['energyMin'],
      forbidden: [
        'bpmMin',
        'bpmMax',
        'danceabilityMin',
        'danceabilityMax',
        'valenceMin',
        'valenceMax',
      ],
      maxPairs: 2,
    },
    derived: { energyMin: 'energy.workout', arousalMin: 'arousal.workout' },
  },
  {
    id: '43',
    set: 'corpus',
    sentence: 'something for the gym',
    expected: { energyMin: 70, arousalMin: 60 },
    must: {
      outcome: 'parse',
      required: ['energyMin'],
      forbidden: ['bpmMin', 'bpmMax'],
      maxPairs: 2,
      rows: 'nonEmpty',
    },
    derived: { energyMin: 'energy.workout', arousalMin: 'arousal.workout' },
  },
  {
    id: '44',
    set: 'corpus',
    sentence: '50 tracks',
    expected: { limit: 50 },
    must: { outcome: 'parse', required: ['limit'], maxPairs: 0, exact: { limit: 50 } },
  },
  {
    id: '45',
    set: 'corpus',
    sentence: 'three hours of ambient',
    expected: { genre: 'ambient', limit: 45 },
    must: {
      outcome: 'parse',
      required: ['genre', 'limit'],
      maxPairs: 0,
      tolerances: { limit: [30, 50] },
    },
  },
  {
    id: '46',
    set: 'corpus',
    sentence: 'the whole library',
    expected: { limit: 100 },
    must: { outcome: 'parse', required: ['limit'], maxPairs: 0, exact: { limit: 100 } },
  },
  {
    id: '47',
    set: 'corpus',
    sentence: 'skip 40',
    expected: { offset: 40 },
    must: { outcome: 'parse', required: ['offset'], maxPairs: 0, exact: { offset: 40 } },
  },
  {
    id: '48',
    set: 'corpus',
    sentence: 'sort by BPM',
    expected: { sort: 'bpm' },
    must: { outcome: 'parse', required: ['sort'], maxPairs: 0, exact: { sort: 'bpm' } },
  },
  {
    id: '49',
    set: 'corpus',
    sentence: 'alphabetical',
    expected: { sort: 'title' },
    must: { outcome: 'parse', required: ['sort'], maxPairs: 0, exact: { sort: 'title' } },
  },
  {
    id: '50',
    set: 'corpus',
    sentence: 'música triste',
    expected: { mood: ['sad'], valenceMax: 40 },
    must: { outcome: 'parse', required: ['mood'], forbidden: ['query'], maxPairs: 1 },
    derived: { valenceMax: 'valence.sad' },
  },
  {
    id: '51',
    set: 'corpus',
    sentence: 'entspannte Musik zum Lesen',
    expected: { mood: ['relaxed'], energyMax: 40 },
    must: { outcome: 'parse', required: ['mood'], forbidden: ['query'], maxPairs: 1 },
    derived: { energyMax: 'energy.chill' },
  },
  {
    id: '52',
    set: 'corpus',
    sentence: '静かな曲',
    expected: { mood: ['relaxed'], energyMax: 40 },
    must: { outcome: 'parse', required: ['mood'], forbidden: ['query'], maxPairs: 1 },
    derived: { energyMax: 'energy.chill' },
  },
  {
    id: '53',
    set: 'corpus',
    sentence: 'play something',
    expected: null,
    must: {
      outcome: 'fail',
      forbidden: ['query', 'bpmMin', 'bpmMax', 'mood', 'genre'],
      note: '{} is the designed answer and parseIntent refuses it: a refusal is correct (§4.4)',
    },
  },
  {
    id: '54',
    set: 'corpus',
    sentence: 'hello',
    expected: null,
    must: {
      outcome: 'either',
      forbidden: [
        'query',
        'genre',
        'key',
        'mood',
        'sort',
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
        'durationMin',
        'durationMax',
      ],
      note: 'the note tolerates a bare limit; anything else is invented',
    },
  },
  {
    id: '55',
    set: 'corpus',
    sentence: 'make it faster',
    expected: null,
    must: {
      outcome: 'fail',
      forbidden: [
        'bpmMin',
        'bpmMax',
        'danceabilityMin',
        'danceabilityMax',
        'valenceMin',
        'valenceMax',
      ],
      note: 'no context carries between asks (ADR 0086)',
    },
  },
  {
    id: '56',
    set: 'corpus',
    sentence: 'anything but country',
    expected: null,
    must: {
      outcome: 'fail',
      forbidden: ['genre'],
      note: 'negation is unsupported; the note\'s !{genre:"country"} is a genre ban',
    },
  },
  {
    id: '57',
    set: 'corpus',
    sentence: "music that isn't sad",
    expected: { mood: ['happy'] },
    must: {
      outcome: 'either',
      forbidden: ['valenceMax'],
      note: 'a refusal or a happy reading both pass; only valence_max is the wrong answer',
    },
  },
  {
    id: '58',
    set: 'corpus',
    sentence: 'songs between 300 and 400 BPM',
    expected: { bpmMin: 180, bpmMax: 180 },
    must: {
      outcome: 'parse',
      required: ['bpmMin', 'bpmMax'],
      maxPairs: 1,
      exact: { bpmMin: 180, bpmMax: 180 },
      note: 'the parser clamps both bounds to the folded tempo range',
    },
  },
  {
    id: '59',
    set: 'corpus',
    sentence: 'add 5 songs to the playlist',
    expected: null,
    must: {
      outcome: 'fail',
      forbidden: ['limit', 'query'],
      note: 'not a search: the sentence asks the App to change a playlist',
    },
  },
  {
    id: '60',
    set: 'corpus',
    sentence: 'play the song from that car video',
    expected: null,
    must: {
      outcome: 'fail',
      forbidden: ['query'],
      note: 'no name is in the sentence, so a query can only be invented',
    },
  },

  /* ------------------------------------------------------- added: N1–N6 */
  {
    id: 'N1',
    set: 'corpus',
    sentence: 'rápido para correr',
    expected: { bpmMin: 125, energyMin: 70, arousalMin: 60 },
    must: {
      outcome: 'parse',
      required: ['bpmMin', 'energyMin'],
      forbidden: ['danceabilityMin', 'danceabilityMax', 'valenceMin', 'valenceMax'],
      maxPairs: 3,
      note: 'an explicit tempo in another language keeps bpm; the purpose adds its own pair',
    },
    derived: { bpmMin: 'bpm.fast', energyMin: 'energy.workout', arousalMin: 'arousal.workout' },
    added: true,
  },
  {
    id: 'N2',
    set: 'corpus',
    sentence: 'tracks under three minutes',
    expected: { durationMax: 180 },
    must: {
      outcome: 'parse',
      required: ['durationMax'],
      forbidden: ['durationMin', 'limit'],
      maxPairs: 1,
      exact: { durationMax: 180 },
      note: 'a track\'s length is duration, not a count',
    },
    added: true,
  },
  {
    id: 'N3',
    set: 'corpus',
    sentence: 'nothing aggressive',
    expected: null,
    must: {
      outcome: 'fail',
      forbidden: ['mood', 'arousalMin', 'arousalMax', 'genre'],
      note: 'a negated mood has no positive filter; a refusal, not an aggressive one',
    },
    added: true,
  },
  {
    id: 'N4',
    set: 'corpus',
    sentence: 'tracks by Daft Punk and Justice',
    expected: { query: 'Daft Punk' },
    must: {
      outcome: 'parse',
      required: ['query'],
      forbidden: ['genre', 'mood', 'bpmMin', 'bpmMax'],
      maxPairs: 0,
      queryOneOf: ['Daft Punk', 'Justice'],
      note: 'the contract has no OR: one name is the best runnable Intent, the conjunction is not',
    },
    contractGap: true,
    added: true,
  },
  {
    id: 'N5',
    set: 'corpus',
    sentence: 'similar to Bonobo',
    expected: { query: 'Bonobo' },
    must: {
      outcome: 'parse',
      required: ['query'],
      forbidden: ['genre', 'mood', ...NO_RANGES],
      maxPairs: 0,
      exact: { query: 'Bonobo' },
      note: 'the prompt names "Similar to X" as a query; the repair must agree',
    },
    added: true,
  },
  {
    id: 'N6',
    set: 'corpus',
    sentence: 'fast techno for a workout',
    expected: { genre: 'techno', bpmMin: 125, energyMin: 70, arousalMin: 60 },
    must: {
      outcome: 'parse',
      required: ['genre', 'bpmMin', 'energyMin'],
      forbidden: ['danceabilityMin', 'danceabilityMax', 'valenceMin', 'valenceMax'],
      maxPairs: 3,
      note: 'the guard sentence §5.1 names but §6 omits: the tempo survives the purpose normalizer',
    },
    derived: { bpmMin: 'bpm.fast', energyMin: 'energy.workout', arousalMin: 'arousal.workout' },
    added: true,
  },
];

/** The twelve sentences the spike measured, as a regression subset. */
export const SPIKE_SENTENCES: readonly string[] = CORPUS.filter((row) => row.set === 'spike').map(
  (row) => row.sentence,
);

export function rowById(id: string): CorpusRow | undefined {
  return CORPUS.find((row) => row.id === id);
}
