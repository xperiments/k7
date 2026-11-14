/**
 * The harness's pure parts. The run loop itself needs Chrome, so it is not tested here; what is
 * tested is the honesty of the export: the fixture file carries only the shipped arm's real
 * answers, and an invalid run is never exported as a model result.
 */

import { describe, expect, it, vi } from 'vitest';

import { parseIntent } from '../playlists/ask';
import { rowById } from './corpus';
import {
  digest,
  exportPayload,
  fixturesFrom,
  loadTarget,
  scoredRunsFor,
  type RunRecord,
  type StoredRun,
} from './harness';
import { scoreRow } from './score';

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
    expect(payload.machine.onDeviceInternals).toBe('variant');
  });
});
