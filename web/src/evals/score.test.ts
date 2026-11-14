/**
 * The rubric, tested without a model. Every fixture is an answer string and what the rubric must
 * say about it; the axis tests pin the arithmetic the report depends on. No test here calls
 * Chrome, and a passing suite is not a quality measurement — it is proof that the judge judges.
 */

import { describe, expect, it } from 'vitest';

import type { LibraryProfile } from '../domain/snapshot';
import { parseIntent } from '../playlists/ask';

import { rowById } from './corpus';
import { FIXTURES } from './fixtures';
import { aggregate, expectedFor, scoreRow, type RowContext, type ScoredRun } from './score';

const LIBRARY: LibraryProfile = {
  tracks: 1495,
  genres: ['techno', 'ambient'],
  keys: ['8A', '9B'],
  moods: ['happy', 'relaxed'],
  duration: { min: 45, p25: 180, p50: 300, p75: 420, max: 900 },
  bpm: { min: 85, p25: 98, p50: 120, p75: 131, max: 172 },
  energy: { min: 12, p25: 40, p50: 60, p75: 74, max: 98 },
  danceability: { min: 10, p25: 35, p50: 55, p75: 72, max: 95 },
  valence: { min: 5, p25: 30, p50: 50, p75: 70, max: 99 },
  arousal: { min: 8, p25: 32, p50: 55, p75: 73, max: 97 },
};

/** The reference library has tracks, so a row that asks for rows can be scored end to end. */
const MATCHED: RowContext = { matched: 5 };

function scored(rowId: string, raw: string, context: RowContext = MATCHED): ScoredRun {
  const row = rowById(rowId);
  if (!row) throw new Error(`no corpus row ${rowId}`);
  return scoreRow(row, raw, parseIntent(raw), context);
}

describe('the fixtures', () => {
  for (const fixture of FIXTURES) {
    it(`${fixture.rowId} ${fixture.origin} is ${fixture.verdict}`, () => {
      const result = scored(fixture.rowId, fixture.raw);
      if (fixture.verdict === 'fail') expect(result.reasons.length).toBeGreaterThan(0);
      expect(result.verdict, result.reasons.join('; ')).toBe(fixture.verdict);
    });
  }
});

describe('a refusal', () => {
  it('scores the empty Intent as correct for every refusal row', () => {
    for (const id of ['53', '54', '55', '56', '59', '60']) {
      const result = scored(id, '{}');
      expect(result.axes.contract, id).toBe(false);
      expect(result.verdict, `${id}: ${result.reasons.join('; ')}`).toBe('pass');
    }
  });

  it('still fails a refusal row that answered with a filter', () => {
    expect(scored('56', '{"genre":"country"}').verdict).toBe('fail');
    expect(scored('55', '{"bpm_min":130}').verdict).toBe('fail');
    expect(scored('59', '{"limit":5}').verdict).toBe('fail');
    expect(scored('60', '{"query":"car video"}').verdict).toBe('fail');
  });
});

describe('the two recorded failures', () => {
  it('fails the workout over-fill on its forbidden pairs and its pair count', () => {
    const overfill = FIXTURES.find((fixture) => fixture.rowId === 'S10' && fixture.origin === 'note-§5.1');
    if (!overfill) throw new Error('the over-fill fixture is missing');
    const result = scored('S10', overfill.raw);
    expect(result.verdict).toBe('fail');
    expect(result.axes.pairs).toBe(5);
    expect(result.axes.forbiddenKeys).toEqual(
      expect.arrayContaining(['bpmMin', 'bpmMax', 'danceabilityMin', 'valenceMin']),
    );
    expect(result.reasons.some((reason) => reason.startsWith('pairs:'))).toBe(true);
  });

  it('fails the Daft Punk answer because the query is not the name', () => {
    const result = scored('S9', '{"query":"songs"}');
    expect(result.verdict).toBe('fail');
    expect(result.axes.queryOk).toBe(false);
    expect(result.axes.recall).toBe(1);
  });
});

describe('the expected Intent', () => {
  it('is the prompt’s own number when a profile is in use', () => {
    const expected = expectedFor(rowById('S4')!, LIBRARY);
    expect(expected).toMatchObject({ valenceMax: 30, bpmMax: 98 });
  });

  it('leaves a sentence-stated number alone', () => {
    expect(expectedFor(rowById('18')!, LIBRARY)).toEqual({ bpmMin: 120, bpmMax: 140 });
  });
});

describe('bounds', () => {
  it('fails an invented second bound beside a one-sided expectation', () => {
    const result = scored('13', '{"bpm_max":100,"bpm_min":90}');
    expect(result.verdict).toBe('fail');
    expect(result.axes.inventedBounds).toEqual(['bpmMin']);
  });

  it('reports a narrowing without failing a tolerated one', () => {
    const result = scored('S1', '{"bpm_min":88,"bpm_max":92}');
    expect(result.verdict).toBe('pass');
    expect(result.axes.bounds).toEqual(
      expect.arrayContaining([{ key: 'bpmMin/bpmMax', problem: 'narrowed' }]),
    );
  });

  it('fails a range that is too vague for its tolerance', () => {
    const result = scored('S1', '{"bpm_min":60,"bpm_max":120}');
    expect(result.verdict).toBe('fail');
    expect(result.axes.bounds).toEqual(
      expect.arrayContaining([{ key: 'bpmMin/bpmMax', problem: 'too-vague' }]),
    );
  });
});

describe('shape and contract', () => {
  it('keeps shape stricter than the contract, and reports both', () => {
    const result = scored('S1', '```json\n{"bpm_min":85,"bpm_max":95}\n```');
    expect(result.axes.shape).toBe(false);
    expect(result.axes.contract).toBe(true);
    expect(result.verdict).toBe('pass');
  });

  it('refuses an object the schema does not allow', () => {
    const result = scored('13', '{"bpm_max":50}');
    expect(result.axes.shape).toBe(false);
    expect(result.axes.contract).toBe(true);
    expect(result.verdict).toBe('fail');
  });
});

describe('precision', () => {
  it('counts a purpose word’s own filter as justified', () => {
    const result = scored('S3', '{"mood":["happy","party"],"danceability_min":70}');
    expect(result.axes.precision).toBe(1);
  });

  it('counts a filter the sentence did not justify', () => {
    const result = scored(
      'S2',
      '{"genre":"techno","mood":["aggressive"],"bpm_min":125,"arousal_min":65,"sort":"bpm"}',
    );
    expect(result.axes.precision).toBe(4 / 5);
    expect(result.verdict).toBe('pass');
  });
});

describe('values', () => {
  it('compares keys by keyCode and as a set', () => {
    expect(scored('29', '{"key":["F#m"]}').verdict).toBe('pass');
    expect(scored('29', '{"key":["F♯m"]}').verdict).toBe('fail');
    expect(scored('31', '{"key":["9A","8A"]}').verdict).toBe('pass');
    expect(scored('31', '{"key":["8A"]}').verdict).toBe('fail');
  });

  it('compares queries case-folded and trimmed', () => {
    expect(scored('S9', '{"query":"  daft punk "}').verdict).toBe('pass');
  });

  it('accepts either name for a sentence naming two artists', () => {
    expect(scored('N4', '{"query":"Justice"}').verdict).toBe('pass');
    expect(scored('N4', '{"query":"daft punk and justice"}').verdict).toBe('fail');
  });

  it('treats a missing tolerance key as a miss, not a pass', () => {
    expect(scored('S7', '{"mood":["relaxed"]}').verdict).toBe('fail');
    expect(scored('S7', '{"mood":["relaxed"],"limit":20}').verdict).toBe('fail');
    expect(scored('S7', '{"mood":["relaxed"],"limit":15}').verdict).toBe('pass');
  });
});

describe('usefulness', () => {
  it('is unscored before a search, and fails when the search is empty', () => {
    expect(scored('S1', '{"bpm_min":85,"bpm_max":95}', {}).verdict).toBe('unscored');
    expect(scored('S1', '{"bpm_min":85,"bpm_max":95}', { matched: 0 }).verdict).toBe('fail');
  });

  it('still fails a bad Intent when the search cannot be run', () => {
    expect(scored('S1', '{"bpm_min":60,"bpm_max":120}', {}).verdict).toBe('fail');
  });
});

describe('aggregate', () => {
  const pass = () => scored('S1', '{"bpm_min":85,"bpm_max":95}');
  const fail = () => scored('S1', '{"bpm_min":60,"bpm_max":120}');

  it('passes a row only on three identical passes', () => {
    const result = aggregate([pass(), pass(), pass()]);
    expect(result).toMatchObject({
      rows: 1,
      completeRows: 1,
      passed: 1,
      failed: 0,
      unstable: 0,
      passRate: 1,
      flakeRate: 0,
    });
  });

  it('calls two passes and a failure unstable, not a pass', () => {
    const result = aggregate([pass(), pass(), fail()]);
    expect(result).toMatchObject({ passed: 0, unstable: 1, flakeRate: 1 });
  });

  it('does not count a contract gap as a model failure', () => {
    const result = aggregate([
      scored('S8', '{"sort":"danceability","limit":20}'),
      scored('S8', '{"sort":"danceability","limit":20}'),
      scored('S8', '{"sort":"danceability","limit":20}'),
    ]);
    expect(result).toMatchObject({
      modelRows: 0,
      passed: 0,
      contractGaps: { rows: 1, passed: 1 },
      passRate: null,
    });
  });

  it('reports refusals, repairs and normalization separately', () => {
    const refusal = scored('53', '{}');
    const result = aggregate([
      { ...refusal, repair: 'notJson', normalized: true },
      refusal,
      refusal,
    ]);
    expect(result.refusals).toEqual({ rows: 1, passed: 1 });
    expect(result.repairs.notJson).toBe(1);
    expect(result.normalizedRuns).toBe(1);
  });
});
