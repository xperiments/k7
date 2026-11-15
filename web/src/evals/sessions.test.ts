/**
 * The session harness's pure parts: the plan it sends, the strategies it offers, and the report
 * it builds. The model calls are Chrome's and are not tested here.
 */

import { describe, expect, it, vi } from 'vitest';

vi.mock('../storage/snapshots', () => ({ newestSnapshot: vi.fn() }));
vi.mock('../storage/media', () => ({ listMedia: vi.fn() }));

import { parseIntent } from '../playlists/ask/parse';

import { rowById } from './corpus';
import { scoreRow, type ScoredRun } from './score';
import {
  sessionPlan,
  strategiesFor,
  strategyDescription,
  strategyLabel,
  summarize,
  type SessionRecord,
  type Strategy,
} from './sessions';

const row = rowById('34')!;
const answer = '{"mood":["sad"],"valence_max":40}';
const baseScored = scoreRow(row, answer, parseIntent(answer), { profile: null });
const scoredAs = (verdict: ScoredRun['verdict']): ScoredRun => ({ ...baseScored, verdict });

function record(
  rowId: string,
  rep: 1 | 2 | 3,
  verdict: ScoredRun['verdict'] | null,
  ms = 1_000,
  strategy: Strategy = 'create',
): SessionRecord {
  const scored = verdict === null ? undefined : scoredAs(verdict);
  return {
    strategy,
    rowId,
    sentence: row.sentence,
    rep,
    answer: scored ? answer : '',
    ms,
    parsed: scored ? {} : { error: 'the call timed out or was stopped', retryable: true },
    ...(scored ? { scored } : { error: 'the call timed out or was stopped' }),
  };
}

describe('sessionPlan', () => {
  it('sends the same system prompt and examples the App does', () => {
    const plan = sessionPlan(null);
    expect(plan.system).toContain('70..180');
    expect(plan.examples).toHaveLength(14);
    expect(plan.initialPrompts).toHaveLength(29);
    expect(plan.initialPrompts[0]).toEqual({ role: 'system', content: plan.system });
    expect(plan.initialPrompts[1]).toEqual({ role: 'user', content: plan.examples[0][0] });
    expect(plan.systemSha).toHaveLength(8);
    expect(plan.examplesSha).toHaveLength(8);
  });
});

describe('strategiesFor', () => {
  it('offers the four base strategies, and sampling only when the trial reports it', () => {
    expect(strategiesFor(false)).toEqual(['create', 'clone', 'create-omit', 'clone-omit']);
    expect(strategiesFor(true)).toContain('sampling');
    expect(strategyLabel('clone-omit')).toBe('clone + omit');
    expect(strategyDescription('sampling')).toContain('Sampling Parameters');
  });
});

describe('summarize', () => {
  const records: SessionRecord[] = [
    // stable pass
    record('A', 1, 'pass', 900),
    record('A', 2, 'pass', 1_000),
    record('A', 3, 'pass', 1_100),
    // stable fail
    record('B', 1, 'fail', 1_200),
    record('B', 2, 'fail', 1_300),
    record('B', 3, 'fail', 1_400),
    // unstable
    record('C', 1, 'pass', 1_500),
    record('C', 2, 'fail', 1_600),
    record('C', 3, 'pass', 1_700),
    // incomplete: two answers
    record('D', 1, 'pass', 1_800),
    record('D', 2, 'pass', 1_900),
    // an error
    record('E', 1, null, 2_000),
  ];
  const summary = summarize(records, 'create');

  it('counts the 3/3 states and the errors', () => {
    expect(summary.records).toBe(12);
    expect(summary.rows).toBe(5);
    expect(summary.pass).toBe(1);
    expect(summary.fail).toBe(1);
    expect(summary.unstable).toBe(1);
    expect(summary.incomplete).toBe(2);
    expect(summary.errors).toBe(1);
  });

  it('reports the latency of the answered calls only', () => {
    expect(summary.meanMs).toBe(1_400);
    expect(summary.medianMs).toBe(1_400);
    expect(summary.p95Ms).toBe(1_900);
    expect(summary.stdMs).toBeGreaterThan(0);
  });

  it('tallies the per-field states and ignores another strategy', () => {
    expect(summary.fields.mood.exact).toBe(11);
    expect(summary.fields.mood.exact + summary.fields.mood.missing).toBeGreaterThan(0);
    expect(summarize([record('A', 1, 'pass', 1, 'clone')], 'create').records).toBe(0);
  });
});
