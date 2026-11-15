/**
 * The grounding experiment, without a model: what each rule would drop from the corpus's expected
 * Intents, and what it would change on the recorded answers. Nothing here is on the shipped path.
 */

import { describe, expect, it } from 'vitest';

import { applySentenceFilters, sentenceFilters } from '../playlists/ask/sentence';

import { CORPUS, rowById } from './corpus';
import { FIXTURES } from './fixtures';
import { auditExpected, compareGrounding, GROUNDING_RULES, ground } from './grounding';

describe('ground', () => {
  it('drops a sort, a limit and a BPM the sentence does not justify', () => {
    expect(
      ground('songs by Daft Punk', { query: 'Daft Punk', sort: 'bpm', limit: 20, bpmMax: 120 }),
    ).toEqual({ query: 'Daft Punk' });
  });

  it('keeps what the sentence states', () => {
    expect(ground('sort by BPM', { sort: 'bpm', bpmMin: 120 })).toEqual({
      sort: 'bpm',
      bpmMin: 120,
    });
    expect(ground('fast aggressive techno', { bpmMin: 125, arousalMin: 65 })).toEqual({
      bpmMin: 125,
      arousalMin: 65,
    });
    const stated = sentenceFilters('10 songs for a workout');
    expect(ground('10 songs for a workout', applySentenceFilters({ limit: 50 }, stated))).toEqual({
      limit: 10,
    });
  });

  it('can run one rule at a time', () => {
    expect(
      ground('songs by Daft Punk', { sort: 'bpm', limit: 20, bpmMax: 120 }, new Set(['sort'])),
    ).toEqual({ limit: 20, bpmMax: 120 });
  });
});

describe('the legitimate-filter audit', () => {
  it('finds the sort and limit rules stripping a corpus filter', () => {
    expect(auditExpected(CORPUS, 'sort').map((drop) => drop.rowId)).toEqual(['S8']);
    expect(auditExpected(CORPUS, 'limit').map((drop) => drop.rowId)).toEqual(['46']);
    expect(auditExpected(CORPUS, 'bpm')).toEqual([]);
  });
});

describe('the recorded answers', () => {
  it('compares each rule over the fixtures', () => {
    for (const rule of GROUNDING_RULES) {
      const report = compareGrounding(FIXTURES, rowById, rule, { matched: 5 });
      expect(report.sampled, rule).toBe(30);
      expect(Object.keys(report.fieldStates), rule).toHaveLength(13);
    }
  });

  it('loses S8 for the sort rule, gains 54 for the limit rule, and leaves the BPM alone', () => {
    const sort = compareGrounding(FIXTURES, rowById, 'sort', { matched: 5 });
    expect(sort.lostRows).toEqual(['S8']);
    expect(sort.gainedRows).toEqual([]);
    expect(sort.lostFields).toBe(1);
    expect(sort.verdictChanges).toEqual([{ rowId: 'S8', before: 'pass', after: 'fail' }]);

    const limit = compareGrounding(FIXTURES, rowById, 'limit', { matched: 5 });
    expect(limit.gainedRows).toEqual(['54']);
    expect(limit.lostRows).toEqual([]);
    expect(limit.gainedFields).toBe(1);

    const bpm = compareGrounding(FIXTURES, rowById, 'bpm', { matched: 5 });
    expect(bpm.gainedFields).toBe(0);
    expect(bpm.lostFields).toBe(0);
    expect(bpm.verdictChanges).toEqual([]);
  });
});
