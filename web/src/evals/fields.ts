/**
 * The per-field view of one Intent: what each field did against the row's expected Intent.
 *
 * The row verdict belongs to `scoreRow`; this module never feeds it. It exists so a change that
 * fixes the query and breaks BPM is visible field by field, which one pass/fail cannot show.
 */

import type { SearchArgs } from '../device/mcp';
import { keyCode } from '../domain/key';

import type { Must, NumericKey } from './corpus';

/* -------------------------------------------------------------------------- */
/* Values                                                                      */
/* -------------------------------------------------------------------------- */

/** One value, compared the way §4.3 says: keys by `keyCode`, strings case-folded, arrays as sets. */
export function matches(key: string, got: unknown, want: unknown): boolean {
  if (Array.isArray(want)) {
    if (!Array.isArray(got)) return false;
    const values = got.filter((item): item is string => typeof item === 'string');
    if (values.length !== got.length) return false;
    const fold = (value: string) => (key === 'key' ? keyCode(value) : value.trim().toLowerCase());
    const left = new Set(values.map(fold));
    const right = new Set((want as unknown[]).map((item) => fold(String(item))));
    if (left.size !== right.size) return false;
    for (const value of right) {
      if (!left.has(value)) return false;
    }
    return true;
  }
  if (typeof want === 'number') return typeof got === 'number' && got === want;
  if (typeof want === 'string') {
    return typeof got === 'string' && got.trim().toLowerCase() === want.trim().toLowerCase();
  }
  return false;
}

/** The fields a person reads in the answer, in the order the breakdown reports them. */
export const FIELD_NAMES = [
  'query',
  'genre',
  'mood',
  'key',
  'bpm',
  'energy',
  'danceability',
  'valence',
  'arousal',
  'duration',
  'limit',
  'offset',
  'sort',
] as const;

export type FieldName = (typeof FIELD_NAMES)[number];

/** Every state a field can be in; the aggregate reports all of them, including the zeroes. */
export const FIELD_STATES = [
  'exact',
  'missing',
  'extra',
  'wrong',
  'wrong-side',
  'narrowed',
  'too-vague',
  'partial',
  'absent',
] as const;

/**
 * What one field did against the row's expected Intent. `wrong` is a value that is present but
 * not the expected one (a query, a count, a shifted pair); `wrong-side` is a bound on the side
 * the row did not ask for; `narrowed` and `too-vague` are the two ways a pair misses a range.
 */
export type FieldState =
  | 'exact'
  | 'missing'
  | 'extra'
  | 'wrong'
  | 'wrong-side'
  | 'narrowed'
  | 'too-vague'
  | 'partial'
  | 'absent';

export type FieldTally = Record<FieldState, number>;

export type FieldValue =
  | string
  | number
  | readonly string[]
  | { readonly min?: number; readonly max?: number };

export interface FieldScore {
  field: FieldName;
  state: FieldState;
  expected?: FieldValue;
  got?: FieldValue;
  /** The purpose table prescribes this extra filter, so the rubric's precision allows it. */
  allowed?: boolean;
  /** A pair's missing side, or a list's missing values. */
  missing?: string[];
  /** A list's values beyond the expected set. */
  extra?: string[];
}

/* -------------------------------------------------------------------------- */
/* The field breakdown                                                         */
/* -------------------------------------------------------------------------- */

const foldText = (value: string): string => value.trim().toLowerCase();

/** A key's identity is the device's code; an unreadable spelling is compared as text. */
function foldKey(value: string): string {
  const code = keyCode(value);
  return code === 0 ? `raw:${foldText(value)}` : `code:${code}`;
}

function wantNumber(
  must: Must,
  expected: Partial<SearchArgs> | null,
  key: keyof SearchArgs,
): number | undefined {
  const exact = must.exact?.[key];
  if (typeof exact === 'number') return exact;
  const value = expected?.[key];
  return typeof value === 'number' ? value : undefined;
}

/** Whether a number is the expected one: `must.exact` wins, then the row's tolerance. */
function exactNumber(must: Must, key: string, got: number, want: number): boolean {
  const exact = must.exact?.[key];
  if (exact !== undefined) return matches(key, got, exact);
  const tolerance = must.tolerances?.[key];
  if (Array.isArray(tolerance)) {
    const [low, high] = tolerance as readonly [number, number];
    return got >= low && got <= high;
  }
  if (typeof tolerance === 'number') return Math.abs(got - want) <= tolerance;
  return got === want;
}

function pairValue(min?: number, max?: number): { min?: number; max?: number } | undefined {
  if (min === undefined && max === undefined) return undefined;
  return { ...(min !== undefined ? { min } : {}), ...(max !== undefined ? { max } : {}) };
}

function pairField(
  field: FieldName,
  minKey: NumericKey,
  maxKey: NumericKey,
  expected: Partial<SearchArgs> | null,
  intent: SearchArgs,
  must: Must,
  allowed: Set<string>,
): FieldScore {
  const wantMin = wantNumber(must, expected, minKey);
  const wantMax = wantNumber(must, expected, maxKey);
  const rawMin = intent[minKey];
  const rawMax = intent[maxKey];
  const gotMin = typeof rawMin === 'number' ? rawMin : undefined;
  const gotMax = typeof rawMax === 'number' ? rawMax : undefined;
  const want = pairValue(wantMin, wantMax);
  const got = pairValue(gotMin, gotMax);
  const base: FieldScore = {
    field,
    state: 'absent',
    ...(want ? { expected: want } : {}),
    ...(got ? { got } : {}),
  };

  if (wantMin === undefined && wantMax === undefined) {
    if (gotMin === undefined && gotMax === undefined) return base;
    return { ...base, state: 'extra', ...(allowed.has(field) ? { allowed: true } : {}) };
  }

  if (wantMin !== undefined && wantMax === undefined) {
    if (gotMax !== undefined) return { ...base, state: 'wrong-side' };
    if (gotMin === undefined) return { ...base, state: 'missing', missing: [minKey] };
    if (exactNumber(must, minKey, gotMin, wantMin)) return { ...base, state: 'exact' };
    return { ...base, state: gotMin > wantMin ? 'narrowed' : 'too-vague' };
  }

  if (wantMax !== undefined && wantMin === undefined) {
    if (gotMin !== undefined) return { ...base, state: 'wrong-side' };
    if (gotMax === undefined) return { ...base, state: 'missing', missing: [maxKey] };
    if (exactNumber(must, maxKey, gotMax, wantMax)) return { ...base, state: 'exact' };
    return { ...base, state: gotMax < wantMax ? 'narrowed' : 'too-vague' };
  }

  if (wantMin !== undefined && wantMax !== undefined) {
    if (gotMin === undefined && gotMax === undefined) {
      return { ...base, state: 'missing', missing: [minKey, maxKey] };
    }
    if (gotMin === undefined || gotMax === undefined) {
      return {
        ...base,
        state: 'partial',
        missing: [gotMin === undefined ? minKey : maxKey],
      };
    }
    const exactMin = exactNumber(must, minKey, gotMin, wantMin);
    const exactMax = exactNumber(must, maxKey, gotMax, wantMax);
    if (exactMin && exactMax) return { ...base, state: 'exact' };
    const narrowed = (gotMin > wantMin && !exactMin) || (gotMax < wantMax && !exactMax);
    const wider = (gotMin < wantMin && !exactMin) || (gotMax > wantMax && !exactMax);
    if (narrowed && wider) return { ...base, state: 'wrong' };
    if (narrowed) return { ...base, state: 'narrowed' };
    if (wider) return { ...base, state: 'too-vague' };
    return { ...base, state: 'wrong' };
  }

  return base;
}

function singleField(
  field: 'limit' | 'offset',
  expected: Partial<SearchArgs> | null,
  intent: SearchArgs,
  must: Must,
  allowed: Set<string>,
): FieldScore {
  const want = wantNumber(must, expected, field);
  const raw = intent[field];
  const got = typeof raw === 'number' ? raw : undefined;
  const base: FieldScore = {
    field,
    state: 'absent',
    ...(want !== undefined ? { expected: want } : {}),
    ...(got !== undefined ? { got } : {}),
  };
  if (want === undefined) {
    if (got === undefined) return base;
    return { ...base, state: 'extra', ...(allowed.has(field) ? { allowed: true } : {}) };
  }
  if (got === undefined) return { ...base, state: 'missing' };
  return { ...base, state: exactNumber(must, field, got, want) ? 'exact' : 'wrong' };
}

function textField(
  field: 'query' | 'genre' | 'sort',
  expected: Partial<SearchArgs> | null,
  intent: SearchArgs,
  must: Must,
  allowed: Set<string>,
): FieldScore {
  let want: string | readonly string[] | undefined;
  if (field === 'query' && must.queryOneOf) {
    want = [...must.queryOneOf];
  } else {
    const value = expected?.[field];
    if (typeof value === 'string') want = value;
  }
  const raw = intent[field];
  const got = typeof raw === 'string' ? raw : undefined;
  const base: FieldScore = {
    field,
    state: 'absent',
    ...(want !== undefined ? { expected: want } : {}),
    ...(got !== undefined ? { got } : {}),
  };
  if (want === undefined) {
    if (got === undefined) return base;
    return { ...base, state: 'extra', ...(allowed.has(field) ? { allowed: true } : {}) };
  }
  if (got === undefined) return { ...base, state: 'missing' };
  const exact =
    typeof want === 'string'
      ? foldText(want) === foldText(got)
      : want.some((option) => foldText(option) === foldText(got));
  return { ...base, state: exact ? 'exact' : 'wrong' };
}

function listField(
  field: 'mood' | 'key',
  expected: Partial<SearchArgs> | null,
  intent: SearchArgs,
  allowed: Set<string>,
): FieldScore {
  const want = expected?.[field];
  const got = intent[field];
  const wantValues = want ?? [];
  const gotValues = got ?? [];
  if (wantValues.length === 0 && gotValues.length === 0) return { field, state: 'absent' };

  const base: FieldScore = {
    field,
    state: 'absent',
    ...(wantValues.length > 0 ? { expected: [...wantValues] } : {}),
    ...(gotValues.length > 0 ? { got: [...gotValues] } : {}),
  };
  if (wantValues.length === 0) {
    return { ...base, state: 'extra', ...(allowed.has(field) ? { allowed: true } : {}) };
  }

  const fold = field === 'key' ? foldKey : foldText;
  const wantSet = new Set(wantValues.map(fold));
  const gotSet = new Set(gotValues.map(fold));
  const missing = wantValues.filter((value) => !gotSet.has(fold(value)));
  const extra = gotValues.filter((value) => !wantSet.has(fold(value)));
  const state: FieldState =
    missing.length > 0 ? 'missing' : extra.length > 0 ? 'extra' : 'exact';
  return {
    ...base,
    state,
    ...(missing.length > 0 ? { missing } : {}),
    ...(extra.length > 0 ? { extra, ...(allowed.has(field) ? { allowed: true } : {}) } : {}),
  };
}

/** The per-field view of one Intent. It never feeds the verdict; it is the side of the record a person reads. */
export function scoreFields(
  expected: Partial<SearchArgs> | null,
  intent: SearchArgs,
  must: Must,
  allowed: Set<string>,
): FieldScore[] {
  const pair = (
    field: FieldName,
    minKey: NumericKey,
    maxKey: NumericKey,
  ): FieldScore => pairField(field, minKey, maxKey, expected, intent, must, allowed);
  return [
    textField('query', expected, intent, must, allowed),
    textField('genre', expected, intent, must, allowed),
    listField('mood', expected, intent, allowed),
    listField('key', expected, intent, allowed),
    pair('bpm', 'bpmMin', 'bpmMax'),
    pair('energy', 'energyMin', 'energyMax'),
    pair('danceability', 'danceabilityMin', 'danceabilityMax'),
    pair('valence', 'valenceMin', 'valenceMax'),
    pair('arousal', 'arousalMin', 'arousalMax'),
    pair('duration', 'durationMin', 'durationMax'),
    singleField('limit', expected, intent, must, allowed),
    singleField('offset', expected, intent, must, allowed),
    textField('sort', expected, intent, must, allowed),
  ];
}
