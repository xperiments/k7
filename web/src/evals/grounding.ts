/**
 * The general-grounding experiment: drop a model filter the sentence never justified on every
 * sentence, not only a purpose. It is not on the shipped path; the report decides what may ship.
 *
 * The three rules are measured apart because they carry different risks: a sort or a limit can be
 * legitimate without a sort word or a number ("the 20 most danceable tracks", "the whole
 * library"), while a tempo is usually stated. `auditExpected` runs the rules over the corpus's
 * expected Intents to find the legitimate filters they would strip; `compareGrounding` runs them
 * over the recorded answers to see what a model answer would gain or lose.
 */

import type { SearchArgs } from '../device/mcp';
import type { LibraryProfile } from '../domain/snapshot';
import type { IntentFailure } from '../playlists/ask/contract';
import { normalizeKeys } from '../playlists/ask/keys';
import { parseIntent } from '../playlists/ask/parse';
import { hasTempo, normalizePurpose, statesSort } from '../playlists/ask/purpose';
import { applySentenceFilters, sentenceFilters } from '../playlists/ask/sentence';

import type { CorpusRow } from './corpus';
import {
  FIELD_NAMES,
  FIELD_STATES,
  type FieldName,
  type FieldScore,
  type FieldTally,
} from './fields';
import type { Fixture } from './fixtures';
import { expectedFor, scoreRow, type RowContext, type ScoredRun, type Verdict } from './score';

/* -------------------------------------------------------------------------- */
/* The rules                                                                   */
/* -------------------------------------------------------------------------- */

export const GROUNDING_RULES = ['sort', 'limit', 'bpm'] as const;
export type GroundingRule = (typeof GROUNDING_RULES)[number];

/**
 * Drop a filter the sentence does not justify. A count or a session keeps the limit (APP-33's
 * extractor has already applied the stated one); a tempo word or a stated BPM keeps the bounds;
 * a sort word keeps the sort. The rules are independent so the experiment can measure each.
 */
export function ground(
  sentence: string,
  intent: SearchArgs,
  rules: ReadonlySet<GroundingRule> = new Set(GROUNDING_RULES),
): SearchArgs {
  const out: SearchArgs = { ...intent };
  const stated = sentenceFilters(sentence);

  if (rules.has('sort') && !statesSort(sentence)) delete out.sort;
  if (rules.has('limit') && stated.limit === undefined) delete out.limit;
  if (
    rules.has('bpm') &&
    stated.bpmMin === undefined &&
    stated.bpmMax === undefined &&
    !hasTempo(sentence)
  ) {
    delete out.bpmMin;
    delete out.bpmMax;
  }
  return out;
}

/* -------------------------------------------------------------------------- */
/* What the rules would strip from the corpus                                  */
/* -------------------------------------------------------------------------- */

export interface ExpectedDrop {
  rowId: string;
  sentence: string;
  field: GroundingRule;
  expected: string | number;
}

/**
 * The corpus rows whose expected Intent carries a field the rule would drop. This is the
 * legitimate-filter audit: an empty list for a rule means it did not strip a filter the corpus
 * says the sentence had.
 */
export function auditExpected(
  rows: readonly CorpusRow[],
  rule: GroundingRule,
): ExpectedDrop[] {
  const drops: ExpectedDrop[] = [];
  for (const row of rows) {
    const expected = expectedFor(row, null);
    if (!expected) continue;
    const grounded = ground(row.sentence, expected, new Set([rule]));

    if (rule === 'sort' && expected.sort !== undefined && grounded.sort === undefined) {
      drops.push({ rowId: row.id, sentence: row.sentence, field: 'sort', expected: expected.sort });
    }
    if (rule === 'limit' && expected.limit !== undefined && grounded.limit === undefined) {
      drops.push({
        rowId: row.id,
        sentence: row.sentence,
        field: 'limit',
        expected: expected.limit,
      });
    }
    if (
      rule === 'bpm' &&
      (expected.bpmMin !== undefined || expected.bpmMax !== undefined) &&
      grounded.bpmMin === undefined &&
      grounded.bpmMax === undefined
    ) {
      drops.push({
        rowId: row.id,
        sentence: row.sentence,
        field: 'bpm',
        expected: `${expected.bpmMin ?? ''}..${expected.bpmMax ?? ''}`,
      });
    }
  }
  return drops;
}

/* -------------------------------------------------------------------------- */
/* What the rules would change on the recorded answers                         */
/* -------------------------------------------------------------------------- */

export interface GroundingRow {
  rowId: string;
  sentence: string;
  before: ScoredRun;
  after: ScoredRun;
  gained: FieldName[];
  lost: FieldName[];
  verdictChanged: boolean;
}

export interface GroundingReport {
  rule: GroundingRule;
  sampled: number;
  rows: GroundingRow[];
  fieldStates: Record<FieldName, { before: FieldTally; after: FieldTally }>;
  gainedRows: string[];
  lostRows: string[];
  gainedFields: number;
  lostFields: number;
  verdictChanges: { rowId: string; before: Verdict; after: Verdict }[];
}

/** `exact` or `absent` is a field at rest; an `extra` the purpose table allows is not an error. */
function good(field: FieldScore): boolean {
  return (
    field.state === 'exact' ||
    field.state === 'absent' ||
    (field.state === 'extra' && field.allowed === true)
  );
}

function tally(): FieldTally {
  return Object.fromEntries(FIELD_STATES.map((state) => [state, 0])) as FieldTally;
}

/** The shipped Intent for a recorded answer, without the query repair: the grounder is the variable. */
function shippedIntent(
  sentence: string,
  raw: string,
  profile: LibraryProfile | null,
): SearchArgs | IntentFailure {
  const parsed = parseIntent(raw);
  if ('error' in parsed) return parsed;
  return normalizePurpose(
    sentence,
    applySentenceFilters(normalizeKeys(sentence, parsed), sentenceFilters(sentence)),
    profile,
  );
}

/**
 * Run one rule over the recorded fixtures. The before is the shipped Intent; the after is the
 * same Intent with the rule applied. The per-field states say what moved; the row verdict is the
 * frozen rubric's and only the change is reported.
 */
export function compareGrounding(
  fixtures: readonly Fixture[],
  rowsById: (id: string) => CorpusRow | undefined,
  rule: GroundingRule,
  context: RowContext = {},
  profile: LibraryProfile | null = null,
): GroundingReport {
  const rows: GroundingRow[] = [];
  const gainedRows = new Set<string>();
  const lostRows = new Set<string>();
  const verdictChanges: GroundingReport['verdictChanges'] = [];
  const fieldStates = Object.fromEntries(
    FIELD_NAMES.map((field) => [field, { before: tally(), after: tally() }]),
  ) as Record<FieldName, { before: FieldTally; after: FieldTally }>;
  let gainedFields = 0;
  let lostFields = 0;

  for (const fixture of fixtures) {
    const row = rowsById(fixture.rowId);
    if (!row) continue;

    const beforeIntent = shippedIntent(row.sentence, fixture.raw, profile);
    const afterIntent =
      'error' in beforeIntent
        ? beforeIntent
        : ground(row.sentence, beforeIntent, new Set([rule]));
    const before = scoreRow(row, fixture.raw, beforeIntent, context);
    const after = scoreRow(row, fixture.raw, afterIntent, context);

    const beforeFields = new Map(before.fields.map((field) => [field.field, field]));
    const gained: FieldName[] = [];
    const lost: FieldName[] = [];
    for (const field of after.fields) {
      const was = beforeFields.get(field.field);
      if (!was) continue;
      fieldStates[field.field].before[was.state] += 1;
      fieldStates[field.field].after[field.state] += 1;
      if (!good(was) && good(field)) gained.push(field.field);
      if (good(was) && !good(field)) lost.push(field.field);
    }
    if (gained.length > 0) {
      gainedRows.add(row.id);
      gainedFields += gained.length;
    }
    if (lost.length > 0) {
      lostRows.add(row.id);
      lostFields += lost.length;
    }
    if (before.verdict !== after.verdict) {
      verdictChanges.push({ rowId: row.id, before: before.verdict, after: after.verdict });
    }
    rows.push({
      rowId: row.id,
      sentence: row.sentence,
      before,
      after,
      gained,
      lost,
      verdictChanged: before.verdict !== after.verdict,
    });
  }

  return {
    rule,
    sampled: rows.length,
    rows,
    fieldStates,
    gainedRows: [...gainedRows],
    lostRows: [...lostRows],
    gainedFields,
    lostFields,
    verdictChanges,
  };
}
