/**
 * The harness's pure parts. The run loop itself needs Chrome, so it is not tested here; what is
 * tested is the honesty of the export: the fixture file carries only the shipped arm's real
 * answers, and an invalid run is never exported as a model result.
 */

import { describe, expect, it, vi } from 'vitest';

import { parseIntent } from '../playlists/ask/parse';
import { CORPUS, rowById } from './corpus';
import {
  acceptStableVerdicts,
  dedupeRecords,
  digest,
  exportPayload,
  finishedReps,
  fixturesFrom,
  intentChanged,
  isSampled,
  isTimeoutError,
  loadTarget,
  rescoreRun,
  retryTimeout,
  reviewKind,
  runKey,
  scoredRunsFor,
  upsertRecord,
  withTimeout,
  type RunRecord,
  type StoredRun,
} from './harness';
import { FIELD_NAMES } from './fields';
import { scoreRow, type Verdict } from './score';

/* The harness imports the image store and the media store for scopes 2 and 3. This test runs in
 * node with no IndexedDB, and scope `static` never reads either, so they are stubbed out. */
vi.mock('../storage/snapshots', () => ({ newestSnapshot: vi.fn() }));
vi.mock('../storage/media', () => ({ listMedia: vi.fn() }));

const RAW = '{"bpm_min":85,"bpm_max":95}';

function record(overrides: Partial<RunRecord> = {}): RunRecord {
  const row = rowById('S1');
  if (!row) throw new Error('no S1');
  const parsed = parseIntent(RAW);
  return {
    id: 'requestIntent@static:S1:1',
    rowId: 'S1',
    set: 'spike',
    sentence: row.sentence,
    arm: 'requestIntent',
    scope: 'static',
    rep: 1,
    profile: { kind: 'none' },
    examples: { count: 9, sha: '00000000' },
    systemSha: '00000000',
    schemaSha: '00000000',
    systemChars: 1,
    availability: 'available',
    at: '2026-11-14T00:00:00.000Z',
    raw: RAW,
    attempts: [],
    calls: 1,
    repair: 'none',
    parsed,
    final: parsed,
    schemaOk: true,
    normalized: false,
    latencyMs: 1,
    scored: scoreRow(row, RAW, parsed, { matched: 5 }),
    verdictHuman: null,
    note: '',
    invalid: false,
    ...overrides,
  };
}

describe('digest', () => {
  it('is stable and notices a changed string', () => {
    expect(digest('abc')).toBe(digest('abc'));
    expect(digest('abc')).not.toBe(digest('abd'));
  });
});

describe('withTimeout', () => {
  it('gives up on an answer that never comes', async () => {
    await expect(withTimeout(new Promise(() => {}), 20)).rejects.toThrow(/no answer after/);
  });

  it('passes an answer through', async () => {
    await expect(withTimeout(Promise.resolve('ok'), 1_000)).resolves.toBe('ok');
  });

  it('aborts the work it gives up on', async () => {
    let aborted = false;
    await expect(
      withTimeout(
        new Promise(() => {}),
        20,
        () => {
          aborted = true;
        },
      ),
    ).rejects.toThrow(/no answer after/);
    expect(aborted).toBe(true);
  });
});

describe('retryTimeout', () => {
  const timeout = { invalid: true, error: 'no answer after 90 s' };
  const answer = { invalid: false, error: undefined };

  it('recognises only the timeout error', () => {
    expect(isTimeoutError('no answer after 90 s')).toBe(true);
    expect(isTimeoutError('the model is unavailable')).toBe(false);
    expect(isTimeoutError(undefined)).toBe(false);
  });

  it('retries a timeout and keeps the answer that finally comes', async () => {
    let calls = 0;
    const retried: number[] = [];
    const { result, retries } = await retryTimeout(
      async () => {
        calls += 1;
        return calls < 3 ? timeout : answer;
      },
      { delayMs: 0, onRetry: (retry) => retried.push(retry) },
    );
    expect(result.invalid).toBe(false);
    expect(retries).toBe(2);
    expect(calls).toBe(3);
    expect(retried).toEqual([1, 2]);
  });

  it('does not retry a refusal or an unavailable model', async () => {
    let calls = 0;
    const { result, retries } = await retryTimeout(
      async () => {
        calls += 1;
        return { invalid: true, error: 'the model is unavailable' };
      },
      { delayMs: 0 },
    );
    expect(result.invalid).toBe(true);
    expect(retries).toBe(0);
    expect(calls).toBe(1);
  });

  it('stops at the limit and honours a cancel', async () => {
    let calls = 0;
    const limited = await retryTimeout(
      async () => {
        calls += 1;
        return timeout;
      },
      { retries: 1, delayMs: 0 },
    );
    expect(limited.retries).toBe(1);
    expect(calls).toBe(2);

    calls = 0;
    const cancelled = await retryTimeout(
      async () => {
        calls += 1;
        return timeout;
      },
      { delayMs: 0, canRetry: () => false },
    );
    expect(cancelled.retries).toBe(0);
    expect(calls).toBe(1);
  });
});

describe('run identity', () => {
  it('is the combination, the sentence and the repetition', () => {
    expect(runKey({ arm: 'requestIntent', scope: 'static' }, 'S1', 3)).toBe(
      'requestIntent@static:S1:3',
    );
  });

  it('replaces a re-run instead of appending a second copy of the same rep', () => {
    const first = record();
    const second = record({ note: 're-run' });
    const next = upsertRecord([first], second);
    expect(next).toHaveLength(1);
    expect(next[0].note).toBe('re-run');
  });

  it('repairs a saved run that already holds duplicates, keeping the last', () => {
    const first = record({ note: 'first' });
    const last = record({ note: 'last' });
    const repaired = dedupeRecords([first, first, last]);
    expect(repaired).toHaveLength(1);
    expect(repaired[0].note).toBe('last');
  });

  it('skips a finished rep, re-runs an invalid one and ignores another combination', () => {
    const combo = { arm: 'requestIntent', scope: 'static' } as const;
    const done = record();
    const invalid = record({ rep: 2, invalid: true });
    const otherArm = record({ rep: 3, arm: 'systemOnly' });
    const finished = finishedReps([done, invalid, otherArm], combo);
    expect([...finished]).toEqual(['S1:1']);
  });
});

describe('the audit rule', () => {
  const combo = { arm: 'requestIntent', scope: 'static' } as const;
  const run = (rowId: string, rep: 1 | 2 | 3, verdict: Verdict) =>
    record({
      id: `requestIntent@static:${rowId}:${rep}`,
      rowId,
      rep,
      scored: { ...record().scored!, verdict },
    });

  it('samples deterministically, and not every row', () => {
    const sampled = CORPUS.find((row) => isSampled(row.id, combo));
    expect(sampled).toBeDefined();
    expect(isSampled(sampled!.id, combo)).toBe(true);
    expect(isSampled('S1', combo)).toBe(false);
  });

  it('separates stable, unstable, sampled and gap rows', () => {
    expect(reviewKind('S1', [run('S1', 1, 'pass'), run('S1', 2, 'pass'), run('S1', 3, 'pass')], combo)).toBe('stable');
    expect(reviewKind('S1', [run('S1', 1, 'pass'), run('S1', 2, 'fail'), run('S1', 3, 'pass')], combo)).toBe('unstable');
    expect(reviewKind('S1', [run('S1', 1, 'unscored'), run('S1', 2, 'unscored'), run('S1', 3, 'unscored')], combo)).toBe('unscored');
    const sampled = CORPUS.find((row) => isSampled(row.id, combo))!;
    expect(reviewKind(sampled.id, [run(sampled.id, 1, 'pass'), run(sampled.id, 2, 'pass'), run(sampled.id, 3, 'pass')], combo)).toBe('sample');
    const gap = [run('S1', 1, 'pass'), run('S1', 2, 'pass'), run('S1', 3, 'pass')].map((r) => ({
      ...r,
      scored: { ...r.scored!, contractGap: true },
    }));
    expect(reviewKind('S1', gap, combo)).toBe('gap');
  });

  it('accepts stable rows, leaves mixed rows, samples and human marks alone', () => {
    const stable = [run('S1', 1, 'pass'), run('S1', 2, 'pass'), run('S1', 3, 'pass')];
    const mixed = [run('S2', 1, 'pass'), run('S2', 2, 'fail'), run('S2', 3, 'pass')];
    const result = acceptStableVerdicts([...stable, ...mixed], combo);
    expect(result.accepted).toBe(1);
    expect(
      result.records
        .filter((record) => record.rowId === 'S1')
        .every((record) => record.verdictHuman === 'right' && record.verdictSource === 'accepted-auto'),
    ).toBe(true);
    expect(result.records.filter((record) => record.rowId === 'S2').every((record) => record.verdictHuman === null)).toBe(true);

    const marked = stable.map((record) => ({
      ...record,
      verdictHuman: 'wrong' as const,
      verdictSource: 'human' as const,
    }));
    expect(acceptStableVerdicts(marked, combo).accepted).toBe(0);

    const sampled = CORPUS.find((row) => isSampled(row.id, combo))!;
    const sampleRuns = [run(sampled.id, 1, 'pass'), run(sampled.id, 2, 'pass'), run(sampled.id, 3, 'pass')];
    expect(acceptStableVerdicts(sampleRuns, combo).accepted).toBe(0);
  });
});

describe('rescoreRun', () => {
  it('replaces a stored verdict with the current rules, without a model', () => {
    const row = rowById('19');
    if (!row) throw new Error('no row 19');
    const raw = '{"genre":"electronic"}';
    const parsed = parseIntent(raw);
    const stale = { ...scoreRow(row, raw, parsed, { matched: 5 }), verdict: 'pass' as const };
    const stored = record({
      id: 'requestIntent@static:19:1',
      rowId: '19',
      sentence: row.sentence,
      raw,
      parsed,
      final: parsed,
      scored: stale,
      search: { matched: 5, tracks: 5, truncated: false },
    });
    const result = rescoreRun([stored], null);
    expect(result.rescored).toBe(1);
    expect(result.records[0].scored?.verdict).toBe('fail');
  });

  it('re-scores the Intent that ran beside the model Intent', () => {
    const row = rowById('S1');
    if (!row) throw new Error('no S1');
    const raw = '{"bpm_min":85,"bpm_max":95}';
    const parsed = { bpmMin: 85, bpmMax: 95 };
    const final = { bpmMin: 85 };
    const stored = record({
      raw,
      parsed,
      final,
      scored: scoreRow(row, raw, parsed, { matched: 5 }),
      search: { matched: 5, tracks: 5, truncated: false },
    });
    const result = rescoreRun([stored], null);
    expect(result.records[0].scored?.verdict).toBe('pass');
    expect(result.records[0].effective?.verdict).toBe('fail');
  });

  it('skips an image run whose profile is gone', () => {
    const image = record({ scope: 'image', profile: { kind: 'image', generation: 1 } });
    const result = rescoreRun([image], null);
    expect(result.skipped).toBe(1);
    expect(result.records[0].scored?.verdict).toBe(image.scored?.verdict);
  });
});

describe('intentChanged', () => {
  it('notices a filter the normalizer removed, and nothing else', () => {
    expect(intentChanged({ bpmMax: 95 }, { bpmMax: 95 })).toBe(false);
    expect(intentChanged({ bpmMax: 95 }, {})).toBe(true);
    expect(intentChanged({ mood: ['happy'] }, { mood: ['happy'] })).toBe(false);
    expect(intentChanged({ mood: ['happy'] }, { mood: ['party'] })).toBe(true);
  });
});

describe('scope static', () => {
  it('needs no image', async () => {
    await expect(loadTarget('static')).resolves.toEqual({ profile: null });
  });
});

describe('fixturesFrom', () => {
  it('exports only the shipped arm, on the static scope, with its own verdict', () => {
    const exported = fixturesFrom([record()]);
    expect(exported).toHaveLength(1);
    expect(exported[0]).toMatchObject({ rowId: 'S1', raw: RAW, origin: 'recorded', verdict: 'pass' });
  });

  it('never exports an invalid run, a control arm or an unscored row', () => {
    expect(fixturesFrom([record({ invalid: true })])).toHaveLength(0);
    expect(fixturesFrom([record({ arm: 'systemOnly' })])).toHaveLength(0);
    expect(fixturesFrom([record({ scope: 'image' })])).toHaveLength(0);
    const unscored = record({ scored: { ...record().scored!, verdict: 'unscored' } });
    expect(fixturesFrom([unscored])).toHaveLength(0);
  });
});

describe('scoredRunsFor', () => {
  it('keeps one combination and carries the repair with it', () => {
    const runs = scoredRunsFor(
      [record({ repair: 'queryNotName' }), record({ arm: 'systemOnly' })],
      { arm: 'requestIntent', scope: 'static' },
    );
    expect(runs).toHaveLength(1);
    expect(runs[0].repair).toBe('queryNotName');
  });
});

describe('exportPayload', () => {
  it('is one JSON blob with the matrix and every record', () => {
    const run: StoredRun = {
      startedAt: '2026-11-14T00:00:00.000Z',
      machine: {
        userAgent: 'test',
        hardwareConcurrency: 8,
        deviceMemory: 16,
        onDeviceInternals: 'variant',
      },
      settings: { timeoutSeconds: 90 },
      records: [record()],
    };
    const payload = JSON.parse(exportPayload(run));
    expect(payload).toMatchObject({
      kind: 'k7-ask-evals',
      version: 1,
      rows: 66,
      repetitions: 3,
    });
    expect(payload.matrix).toHaveLength(5);
    expect(payload.records).toHaveLength(1);
    expect(payload.records[0].scored.fields).toHaveLength(FIELD_NAMES.length);
    expect(payload.machine.onDeviceInternals).toBe('variant');
    expect(payload.settings.timeoutSeconds).toBe(90);
  });
});
