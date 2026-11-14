/**
 * The corpus is the contract, so it is tested like one: the ids, the twelve sentences verbatim,
 * every predicate key that exists, the porting fixes, and the Word→Filter numbers agreeing with
 * the prompt they were read from. The spec's table is checked against this file, so freezing the
 * rubric in prose cannot drift from the data the harness runs.
 */

import { readFileSync } from 'node:fs';

import { describe, expect, it } from 'vitest';

import type { SearchArgs } from '../device/mcp';
import { keyCode } from '../domain/key';
import { factsOf } from '../playlists/ask/facts';
import { buildSystemPrompt } from '../playlists/ask/prompt';
import { PURPOSE_RULES } from '../playlists/ask/purpose';
import { sentenceFilters } from '../playlists/ask/sentence';

import { CORPUS, type CorpusRow } from './corpus';
import { expectedFor } from './score';

const SEARCH_KEYS = new Set<keyof SearchArgs>([
  'query',
  'genre',
  'bpmMin',
  'bpmMax',
  'durationMin',
  'durationMax',
  'energyMin',
  'energyMax',
  'danceabilityMin',
  'danceabilityMax',
  'valenceMin',
  'valenceMax',
  'arousalMin',
  'arousalMax',
  'key',
  'mood',
  'sort',
  'offset',
  'limit',
]);

const SPIKE = [
  'tracks between 85 and 95 BPM',
  'fast aggressive techno',
  'happy party songs',
  'sad and slow',
  'something in A minor',
  'Camelot 8A',
  'about an hour of relaxed music',
  'the 20 most danceable tracks',
  'songs by Daft Punk',
  '10 songs for a workout',
  'música alegre para bailar',
  'de la musique calme pour lire',
];

function row(id: string): CorpusRow {
  const found = CORPUS.find((item) => item.id === id);
  if (!found) throw new Error(`no corpus row ${id}`);
  return found;
}

describe('the corpus', () => {
  it('is the note’s sixty plus the six added rows', () => {
    expect(CORPUS).toHaveLength(66);
    expect(CORPUS.filter((item) => item.set === 'spike')).toHaveLength(12);
    expect(CORPUS.filter((item) => item.added === true)).toHaveLength(6);
    expect(new Set(CORPUS.map((item) => item.id)).size).toBe(66);
  });

  it('keeps S1–S12 verbatim as the regression subset', () => {
    expect(CORPUS.filter((item) => item.set === 'spike').map((item) => item.sentence)).toEqual(
      SPIKE,
    );
  });

  it('names only filters that exist', () => {
    for (const item of CORPUS) {
      for (const key of Object.keys(item.expected ?? {})) {
        expect(SEARCH_KEYS.has(key as keyof SearchArgs), `${item.id}: expected.${key}`).toBe(true);
      }
      for (const key of item.must.required ?? []) {
        expect(SEARCH_KEYS.has(key as keyof SearchArgs), `${item.id}: required.${key}`).toBe(true);
      }
      for (const key of item.must.forbidden ?? []) {
        expect(SEARCH_KEYS.has(key as keyof SearchArgs), `${item.id}: forbidden.${key}`).toBe(true);
      }
      for (const key of Object.keys(item.must.exact ?? {})) {
        expect(item.expected, `${item.id}: exact.${key} without an expected Intent`).toHaveProperty(
          key,
        );
      }
      for (const key of Object.keys(item.must.tolerances ?? {})) {
        expect(
          item.expected,
          `${item.id}: tolerances.${key} without an expected Intent`,
        ).toHaveProperty(key);
      }
      for (const key of Object.keys(item.derived ?? {})) {
        expect(item.expected, `${item.id}: derived.${key} without an expected Intent`).toHaveProperty(
          key,
        );
      }
    }
  });

  it('expects a refusal exactly where the sentence has no runnable filter', () => {
    const refusals = CORPUS.filter((item) => item.must.outcome !== 'parse').map((item) => item.id);
    expect(refusals.sort()).toEqual(['53', '54', '55', '56', '57', '59', '60', 'N3']);
    for (const id of refusals) {
      expect(['fail', 'either']).toContain(row(id).must.outcome);
    }
  });


  it('reads only the numbers and keys the corpus states', () => {
    for (const item of CORPUS) {
      const expected = expectedFor(item, null);
      /* A refusal has no filter to extract; the request path never applies the extractor to one. */
      if (!expected) continue;
      const found = sentenceFilters(item.sentence);
      for (const [key, value] of Object.entries(found)) {
        if (key === 'key') {
          const want = new Set((expected.key ?? []).map(keyCode));
          const got = new Set((value as string[]).map(keyCode));
          expect([...got].sort(), `${item.id}: key`).toEqual([...want].sort());
        } else {
          expect(expected[key as keyof SearchArgs], `${item.id}: ${key}`).toEqual(value);
        }
      }
    }
  });

  it('marks the two Intent-level contract gaps without counting them as model failures', () => {
    expect(row('S8').contractGap).toBe(true);
    expect(row('N4').contractGap).toBe(true);
  });

  it('fixes the rows the note wrote ambiguously', () => {
    /* 17: "very slow" was "≤95", which is not a number. The prompt fixes it at bpm_max 95. */
    expect(row('17').expected).toEqual({ bpmMax: 95 });
    expect(row('17').must.forbidden).toEqual(['bpmMin']);
    /* 18: the assertion is on the parsed pair, which the parser swaps. */
    expect(row('18').must.exact).toEqual({ bpmMin: 120, bpmMax: 140 });
    /* 57: parse-or-fail, and only valence_max is the wrong reading. */
    expect(row('57').must.outcome).toBe('either');
    expect(row('57').must.forbidden).toEqual(['valenceMax']);
    /* 58: both bounds clamp to the folded tempo range. */
    expect(row('58').must.exact).toEqual({ bpmMin: 180, bpmMax: 180 });
    /* 22 and 56 encoded value bans the predicate cannot express as keys. */
    expect(row('22').must.forbidden).toContain('query');
    expect(row('56').must.forbidden).toEqual(['genre']);
  });

  it('tests the guard sentence the note names but its table omits', () => {
    expect(row('N6').sentence).toBe('fast techno for a workout');
    expect(row('N6').must.required).toContain('bpmMin');
  });

  it('reads every purpose threshold from the prompt it mirrors', () => {
    const facts = factsOf(null);
    const prompt = buildSystemPrompt(null);
    for (const rule of PURPOSE_RULES) {
      const filters = rule.filters(facts);
      if (rule.id === 'relax') {
        expect(prompt).toContain(`mood ["relaxed"], energy_max ${filters.energyMax}`);
      }
      if (rule.id === 'energise') {
        expect(prompt).toContain(
          `energy_min ${filters.energyMin}, arousal_min ${filters.arousalMin}`,
        );
      }
      if (rule.id === 'dance') {
        expect(prompt).toContain(`mood ["party"], danceability_min ${filters.danceabilityMin}`);
      }
    }
  });

  it('is the table the spec froze', () => {
    const spec = readFileSync(
      new URL('../../../docs/design/assistant-evaluation/spec.md', import.meta.url),
      'utf8',
    );
    for (const item of CORPUS) {
      expect(spec, `${item.id} missing from the spec's table`).toContain(
        `| ${item.id} | \`${item.sentence}\` |`,
      );
    }
  });
});
