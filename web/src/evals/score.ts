/**
 * The scorer: one row's raw answer in, the §4 axes out, and the pass/fail verdict the ticket
 * freezes.
 *
 * It judges the **Intent**, not the string. `raw` is kept as evidence and checked separately for
 * shape (Chrome's promise); `parseIntent` is the contract's own judge. Recall and precision count
 * *filters*, so a `*_min`/`*_max` pair is one; bounds are compared per side so an invented bound,
 * a narrowed range and a too-vague one are different findings.
 *
 * Nothing here calls a model or the device: `scoreRow` is pure, and `use` comes in as the number
 * of tracks the reference search returned. That is what makes the rubric a CI-checkable diff.
 */

import type { SearchArgs } from '../device/mcp';
import type { LibraryProfile } from '../domain/snapshot';
import { keyCode } from '../library/imageSource';
import {
  factsOf,
  intentSchema,
  purposeOf,
  type IntentFailure,
  type PromptFacts,
} from '../playlists/ask';

import type { CorpusRow, FactKey, Must, NumericKey } from './corpus';

/* -------------------------------------------------------------------------- */
/* Axes and filters                                                            */
/* -------------------------------------------------------------------------- */

/** The six measurement axes. A pair is one axis; `limit` and `offset` are not axes. */
const AXES = {
  bpm: ['bpmMin', 'bpmMax'],
  duration: ['durationMin', 'durationMax'],
  energy: ['energyMin', 'energyMax'],
  danceability: ['danceabilityMin', 'danceabilityMax'],
  valence: ['valenceMin', 'valenceMax'],
  arousal: ['arousalMin', 'arousalMax'],
} as const satisfies Record<string, readonly [NumericKey, NumericKey]>;

const PAIR_KEYS = new Set<string>(Object.values(AXES).flat());

/** The filter a key belongs to: a pair's two keys are one filter. */
function filterOf(key: string): string {
  if (!PAIR_KEYS.has(key)) return key;
  for (const [axis, keys] of Object.entries(AXES)) {
    if ((keys as readonly string[]).includes(key)) return axis;
  }
  return key;
}

function filtersOf(intent: SearchArgs): Set<string> {
  const out = new Set<string>();
  for (const [key, value] of Object.entries(intent)) {
    if (value !== undefined) out.add(filterOf(key));
  }
  return out;
}

function pairsOf(intent: SearchArgs): number {
  let pairs = 0;
  for (const keys of Object.values(AXES)) {
    if (keys.some((key) => intent[key] !== undefined)) pairs += 1;
  }
  return pairs;
}

/* -------------------------------------------------------------------------- */
/* The expected Intent                                                         */
/* -------------------------------------------------------------------------- */

function factValue(facts: PromptFacts, key: FactKey): number {
  switch (key) {
    case 'bpm.slow':
      return facts.bpm.slow;
    case 'bpm.fast':
      return facts.bpm.fast;
    case 'energy.chill':
      return facts.energy.chill;
    case 'energy.workout':
      return facts.energy.workout;
    case 'danceability.party':
      return facts.danceability.party;
    case 'valence.sad':
      return facts.valence.sad;
    case 'valence.happy':
      return facts.valence.happy;
    case 'arousal.workout':
      return facts.arousal.workout;
    case 'arousal.aggressive':
      return facts.arousal.aggressive;
    case 'duration.short':
      return facts.duration.short;
    case 'duration.long':
      return facts.duration.long;
  }
}

/**
 * The row's expected Intent at this profile. The literal numbers are the static prompt's; every
 * key in `derived` is replaced by the library's own quantity, the same one the prompt states.
 */
export function expectedFor(
  row: CorpusRow,
  profile: LibraryProfile | null = null,
): Partial<SearchArgs> | null {
  if (!row.expected) return null;
  const facts = factsOf(profile);
  const out: Partial<SearchArgs> = { ...row.expected };
  for (const [key, fact] of Object.entries(row.derived ?? {})) {
    if (!fact) continue;
    (out as Record<string, unknown>)[key] = factValue(facts, fact);
  }
  return out;
}

/* -------------------------------------------------------------------------- */
/* Shape                                                                       */
/* -------------------------------------------------------------------------- */

type Schema = {
  type?: string;
  properties?: Record<string, Schema>;
  required?: readonly string[];
  additionalProperties?: boolean;
  items?: Schema;
  enum?: readonly unknown[];
  minimum?: number;
  maximum?: number;
  pattern?: string;
};

/**
 * The keyword subset the Intent schema actually uses. Chrome's supported subset is unverified
 * (§1.3), so this validates the schema we send, not Chrome's promise; a keyword it does not know
 * is ignored rather than guessed at.
 */
function schemaErrors(value: unknown, schema: Schema, path: string): string[] {
  const errors: string[] = [];
  if (schema.type) {
    const actual = Array.isArray(value) ? 'array' : value === null ? 'null' : typeof value;
    if (schema.type === 'integer') {
      if (typeof value !== 'number' || !Number.isInteger(value)) {
        errors.push(`${path || 'value'}: not an integer`);
        return errors;
      }
    } else if (actual !== schema.type) {
      errors.push(`${path || 'value'}: expected ${schema.type}, got ${actual}`);
      return errors;
    }
  }

  if (schema.enum && !schema.enum.some((allowed) => allowed === value)) {
    errors.push(`${path || 'value'}: not one of ${schema.enum.map(String).join(', ')}`);
  }

  if (typeof value === 'number') {
    if (schema.minimum !== undefined && value < schema.minimum) {
      errors.push(`${path}: below ${schema.minimum}`);
    }
    if (schema.maximum !== undefined && value > schema.maximum) {
      errors.push(`${path}: above ${schema.maximum}`);
    }
  }

  if (typeof value === 'string' && schema.pattern) {
    if (!new RegExp(schema.pattern, 'u').test(value)) errors.push(`${path}: does not match ${schema.pattern}`);
  }

  if (schema.type === 'object' && value && typeof value === 'object' && !Array.isArray(value)) {
    const record = value as Record<string, unknown>;
    for (const key of schema.required ?? []) {
      if (record[key] === undefined) errors.push(`${path}${key}: required`);
    }
    for (const [key, child] of Object.entries(record)) {
      const property = schema.properties?.[key];
      if (!property) {
        if (schema.additionalProperties === false) errors.push(`${path}${key}: not a filter`);
        continue;
      }
      errors.push(...schemaErrors(child, property, `${path}${key}.`));
    }
  }

  if (schema.type === 'array' && Array.isArray(value) && schema.items) {
    value.forEach((item, index) => errors.push(...schemaErrors(item, schema.items!, `${path}${index}.`)));
  }

  return errors;
}

interface ShapeResult {
  ok: boolean;
  errors: string[];
}

/** `raw` must be JSON exactly as `responseConstraint` should have made it. */
function shapeOf(raw: string, profile: LibraryProfile | null): ShapeResult {
  let value: unknown;
  try {
    value = JSON.parse(raw);
  } catch {
    return { ok: false, errors: ['not JSON'] };
  }
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    return { ok: false, errors: ['not a JSON object'] };
  }
  const schema = intentSchema(profile) as unknown as Schema;
  const errors = schemaErrors(value, schema, '');
  return { ok: errors.length === 0, errors };
}

/* -------------------------------------------------------------------------- */
/* Values                                                                      */
/* -------------------------------------------------------------------------- */

/** One value, compared the way §4.3 says: keys by `keyCode`, strings case-folded, arrays as sets. */
function matches(key: string, got: unknown, want: unknown): boolean {
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

/* -------------------------------------------------------------------------- */
/* One run                                                                     */
/* -------------------------------------------------------------------------- */

export type Verdict = 'pass' | 'fail' | 'unscored';

export type RepairKind = 'none' | 'notJson' | 'queryNotName';

export interface RowContext {
  /** The profile whose prompt produced the answer, for the narrowed schema and the derived numbers. */
  profile?: LibraryProfile | null;
  /** Tracks the reference search returned, when the row asked for rows. */
  matched?: number;
  truncated?: boolean;
  /** The reference search refused the Intent (an unparseable key, say): it cannot be run. */
  searchError?: string;
}

export interface BoundProblem {
  key: string;
  problem: 'invented' | 'narrowed' | 'too-vague' | 'partial';
}

export interface RowAxes {
  shape: boolean;
  shapeErrors: string[];
  contract: boolean;
  recall: number | null;
  precision: number | null;
  forbiddenOk: boolean;
  forbiddenKeys: string[];
  inventedBounds: string[];
  bounds: BoundProblem[];
  pairs: number;
  maxPairsOk: boolean;
  exactOk: boolean;
  toleranceOk: boolean;
  queryOk: boolean | null;
  usefulness: boolean | null;
}

export interface ScoredRun {
  rowId: string;
  set: CorpusRow['set'];
  contractGap: boolean;
  /** The sentence's correct outcome is a refusal. */
  refusal: boolean;
  verdict: Verdict;
  axes: RowAxes;
  reasons: string[];
  /** Which repair the shipped path needed, when the harness knows. */
  repair?: RepairKind;
  /** The purpose normalizer changed the Intent the model produced. */
  normalized?: boolean;
}

function compareBounds(
  expected: Partial<SearchArgs> | null,
  intent: SearchArgs,
): { bounds: BoundProblem[]; invented: string[] } {
  const bounds: BoundProblem[] = [];
  const invented: string[] = [];
  if (!expected) return { bounds, invented };

  for (const keys of Object.values(AXES)) {
    const [minKey, maxKey] = keys;
    const wantMin = expected[minKey];
    const wantMax = expected[maxKey];
    if (wantMin === undefined && wantMax === undefined) continue;
    const gotMin = intent[minKey];
    const gotMax = intent[maxKey];

    if (wantMin !== undefined && wantMax === undefined) {
      if (gotMax !== undefined) {
        invented.push(maxKey);
        bounds.push({ key: maxKey, problem: 'invented' });
      }
      if (gotMin === undefined) bounds.push({ key: minKey, problem: 'partial' });
    } else if (wantMax !== undefined && wantMin === undefined) {
      if (gotMin !== undefined) {
        invented.push(minKey);
        bounds.push({ key: minKey, problem: 'invented' });
      }
      if (gotMax === undefined) bounds.push({ key: maxKey, problem: 'partial' });
    } else if (wantMin !== undefined && wantMax !== undefined) {
      if (gotMin === undefined || gotMax === undefined) {
        bounds.push({ key: gotMin === undefined ? minKey : maxKey, problem: 'partial' });
      } else if (gotMin > wantMin && gotMax < wantMax) {
        bounds.push({ key: `${minKey}/${maxKey}`, problem: 'narrowed' });
      } else if (gotMin < wantMin || gotMax > wantMax) {
        bounds.push({ key: `${minKey}/${maxKey}`, problem: 'too-vague' });
      }
    }
  }
  return { bounds, invented };
}

/**
 * One row, one answer. `raw` is the string the model returned (the last one, after a repair) and
 * `parsed` is `parseIntent(raw)`; both are passed in so the harness and the tests score the same
 * bytes the App saw.
 */
export function scoreRow(
  row: CorpusRow,
  raw: string,
  parsed: SearchArgs | IntentFailure,
  context: RowContext = {},
): ScoredRun {
  const profile = context.profile ?? null;
  const contract = !('error' in parsed);
  const intent: SearchArgs = contract ? parsed : {};
  const expected = expectedFor(row, profile);
  const must: Must = row.must;
  const reasons: string[] = [];
  /* An Intent the library refuses is not a runnable Intent, whatever its keys look like. */
  if (context.searchError) reasons.push(`search: ${context.searchError}`);

  const shape = shapeOf(raw, profile);
  const parsedFilters = filtersOf(intent);
  const expectedFilters = expected ? filtersOf(expected) : new Set<string>();
  const pairs = pairsOf(intent);

  const forbiddenKeys = (must.forbidden ?? []).filter((key) => intent[key as keyof SearchArgs] !== undefined);
  const { bounds, invented } = contract
    ? compareBounds(expected, intent)
    : { bounds: [], invented: [] };

  let maxPairsOk = true;
  if (must.maxPairs !== undefined && pairs > must.maxPairs) {
    maxPairsOk = false;
    reasons.push(`pairs: ${pairs} > ${must.maxPairs}`);
  }

  let exactOk = true;
  for (const [key, want] of Object.entries(must.exact ?? {})) {
    const got = intent[key as keyof SearchArgs];
    if (!matches(key, got, want)) {
      exactOk = false;
      reasons.push(`exact: ${key} is ${JSON.stringify(got)} not ${JSON.stringify(want)}`);
    }
  }

  let toleranceOk = true;
  for (const [key, tolerance] of Object.entries(must.tolerances ?? {})) {
    const got = intent[key as keyof SearchArgs];
    const want = expected?.[key as keyof SearchArgs];
    const within =
      typeof got === 'number' &&
      typeof want === 'number' &&
      (Array.isArray(tolerance)
        ? got >= (tolerance[0] as number) && got <= (tolerance[1] as number)
        : Math.abs(got - want) <= (tolerance as number));
    if (!within) {
      toleranceOk = false;
      reasons.push(`tolerance: ${key} is ${JSON.stringify(got)} not ${JSON.stringify(tolerance)}`);
    }
  }

  let queryOk: boolean | null = null;
  const options =
    must.queryOneOf ??
    (typeof expected?.query === 'string' ? [expected.query] : undefined);
  if (options && contract) {
    const got = intent.query?.trim().toLowerCase();
    queryOk = got !== undefined && options.some((option) => option.trim().toLowerCase() === got);
    if (!queryOk) reasons.push(`query: ${JSON.stringify(intent.query)} is not ${options.join(' or ')}`);
  }

  for (const key of forbiddenKeys) reasons.push(`forbidden: ${key}`);
  for (const key of invented) reasons.push(`invented bound: ${key}`);
  if (contract) {
    for (const key of must.required ?? []) {
      if (intent[key as keyof SearchArgs] === undefined) reasons.push(`missing: ${key}`);
    }
  }

  let failed = reasons.length > 0;
  if (!failed && must.outcome === 'parse' && !contract) {
    failed = true;
    reasons.push('contract: no runnable Intent');
  }
  if (!failed && must.outcome === 'fail' && contract) {
    failed = true;
    reasons.push('contract: an Intent where a refusal was the correct answer');
  }

  const wantsRows = must.rows === 'nonEmpty';
  const usefulness = wantsRows ? (context.matched === undefined ? null : context.matched > 0) : null;
  if (usefulness === false) reasons.push('rows: nothing matched');
  let verdict: Verdict;
  if (failed) verdict = 'fail';
  else if (usefulness === false) verdict = 'fail';
  else if (usefulness === null && wantsRows) verdict = 'unscored';
  else verdict = 'pass';

  const denom = expectedFilters.size;
  const recall =
    expected && contract && denom > 0
      ? [...expectedFilters].filter((filter) => parsedFilters.has(filter)).length / denom
      : null;

  const allowed = new Set(expectedFilters);
  if (expected) {
    const purpose = purposeOf(row.sentence);
    if (purpose) {
      for (const key of Object.keys(purpose.filters(factsOf(profile)))) allowed.add(filterOf(key));
    }
  }
  const precision =
    expected && contract && parsedFilters.size > 0
      ? [...parsedFilters].filter((filter) => allowed.has(filter)).length / parsedFilters.size
      : null;

  return {
    rowId: row.id,
    set: row.set,
    contractGap: row.contractGap === true,
    refusal: row.expected === null,
    verdict,
    reasons,
    axes: {
      shape: shape.ok,
      shapeErrors: shape.errors,
      contract,
      recall,
      precision,
      forbiddenOk: forbiddenKeys.length === 0,
      forbiddenKeys,
      inventedBounds: invented,
      bounds,
      pairs,
      maxPairsOk,
      exactOk,
      toleranceOk,
      queryOk,
      usefulness,
    },
  };
}

/* -------------------------------------------------------------------------- */
/* Aggregates                                                                  */
/* -------------------------------------------------------------------------- */

export interface Aggregate {
  runs: number;
  rows: number;
  completeRows: number;
  passed: number;
  failed: number;
  unstable: number;
  unscored: number;
  modelRows: number;
  passRate: number | null;
  flakeRate: number | null;
  contractGaps: { rows: number; passed: number };
  meanRecall: number | null;
  meanPrecision: number | null;
  forbiddenRuns: number;
  inventedBoundRuns: number;
  repairs: Record<RepairKind, number>;
  normalizedRuns: number;
  refusals: { rows: number; passed: number };
}

function mean(values: readonly number[]): number | null {
  return values.length === 0 ? null : values.reduce((sum, value) => sum + value, 0) / values.length;
}

/**
 * The per-row verdict across three runs. A row passes only when all three runs pass — the frozen
 * 3/3 rule — so two passes and a failure is `unstable`, not a pass.
 */
export function aggregate(runs: readonly ScoredRun[]): Aggregate {
  const byRow = new Map<string, ScoredRun[]>();
  for (const run of runs) {
    const group = byRow.get(run.rowId) ?? [];
    group.push(run);
    byRow.set(run.rowId, group);
  }

  let passed = 0;
  let failed = 0;
  let unstable = 0;
  let unscored = 0;
  let completeRows = 0;
  let modelRows = 0;
  let gapRows = 0;
  let gapPassed = 0;
  let refusalRows = 0;
  let refusalPassed = 0;

  for (const group of byRow.values()) {
    const verdicts = group.map((run) => run.verdict);
    if (verdicts.length >= 3) completeRows += 1;

    const state: Verdict | 'unstable' =
      verdicts.every((verdict) => verdict === 'pass') && verdicts.length >= 3
        ? 'pass'
        : verdicts.every((verdict) => verdict === 'fail')
          ? 'fail'
          : verdicts.every((verdict) => verdict === 'unscored')
            ? 'unscored'
            : 'unstable';

    const gap = group[0]?.contractGap === true;
    const refusal = group[0]?.refusal === true;
    if (!gap) {
      modelRows += 1;
      if (state === 'pass') passed += 1;
      else if (state === 'fail') failed += 1;
      else if (state === 'unstable') unstable += 1;
      else unscored += 1;
    } else {
      gapRows += 1;
      if (state === 'pass') gapPassed += 1;
    }
    if (refusal) {
      refusalRows += 1;
      if (state === 'pass') refusalPassed += 1;
    }
  }

  const repairs: Record<RepairKind, number> = { none: 0, notJson: 0, queryNotName: 0 };
  let normalizedRuns = 0;
  for (const run of runs) {
    if (run.repair) repairs[run.repair] += 1;
    if (run.normalized) normalizedRuns += 1;
  }

  return {
    runs: runs.length,
    rows: byRow.size,
    completeRows,
    passed,
    failed,
    unstable,
    unscored,
    modelRows,
    passRate: modelRows > 0 ? passed / modelRows : null,
    flakeRate: modelRows > 0 ? unstable / modelRows : null,
    contractGaps: { rows: gapRows, passed: gapPassed },
    meanRecall: mean(runs.map((run) => run.axes.recall).filter((value): value is number => value !== null)),
    meanPrecision: mean(
      runs.map((run) => run.axes.precision).filter((value): value is number => value !== null),
    ),
    forbiddenRuns: runs.filter((run) => run.axes.forbiddenKeys.length > 0).length,
    inventedBoundRuns: runs.filter((run) => run.axes.inventedBounds.length > 0).length,
    repairs,
    normalizedRuns,
    refusals: { rows: refusalRows, passed: refusalPassed },
  };
}
