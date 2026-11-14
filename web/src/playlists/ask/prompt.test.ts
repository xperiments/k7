/**
 * The prompt is the measured input: its numbers move with the profile, and the schema is a
 * request the parser still judges.
 */

import { describe, expect, it } from 'vitest';

import type { SearchArgs } from '../../device/mcp';

import { BPM_RANGE, DURATION_RANGE, MOOD_VALUES, SORT_VALUES } from './contract';
import { parseIntent } from './parse';
import { INTENT_EXAMPLES, INTENT_SCHEMA, buildSystemPrompt, intentExamples, intentSchema } from './prompt';
import { argsOf, TEST_PROFILE as PROFILE } from './test-support';

describe('INTENT_EXAMPLES', () => {
  it('every example answers with an Intent parseIntent accepts', () => {
    const expected: Record<string, SearchArgs | null> = {
      'twenty relaxed tracks for sleeping': { mood: ['relaxed'], energyMax: 35, limit: 20 },
      'two hours of fast techno': { genre: 'techno', bpmMin: 125, limit: 30 },
      'songs by Daft Punk': { query: 'Daft Punk' },
      'something in B minor, danceable': { key: ['Bm'], danceabilityMin: 65 },
      'música tranquila para estudiar': { mood: ['relaxed'], energyMax: 40 },
      'slowest tracks first': { sort: 'bpm' },
      'tracks under three minutes': { durationMax: 180 },
      'long mixes': { durationMin: 480 },
      'play something': null,
    };

    for (const [sentence, answer] of INTENT_EXAMPLES) {
      if (!(sentence in expected)) throw new Error(`no expected Intent for ${sentence}`);
      const want = expected[sentence];
      if (want === null) {
        /* `{}` is the designed no-filter answer, and parseIntent refuses it by design. */
        expect(JSON.parse(answer)).toEqual({});
        continue;
      }
      expect(argsOf(parseIntent(answer))).toEqual(want);
    }
  });
});

describe('buildSystemPrompt', () => {
  it('uses the library the profile describes', () => {
    const prompt = buildSystemPrompt(PROFILE);
    expect(prompt).toContain('85..172');
    expect(prompt).toContain('45..900 (seconds)');
    expect(prompt).toContain('duration_max 180 / duration_min 420');
    expect(prompt).toContain('4120 tracks');
    expect(prompt).toContain('Genres present (most common first): techno, ambient');
    expect(prompt).toContain('energy_max 40');
    expect(prompt).toContain('bpm_min 131 / bpm_max 98');
    expect(prompt).toContain('Keys present (Camelot): 8A, 9B');
    expect(prompt).toContain('Moods present: happy, relaxed');
  });

  it('is the static prompt without a profile', () => {
    const prompt = buildSystemPrompt(null);
    expect(prompt).toContain('70..180');
    expect(prompt).toContain('0..86400 (seconds)');
    expect(prompt).not.toContain('THIS LIBRARY');
  });
});

describe('intentSchema', () => {
  it('closes the genres, moods and ranges to the library', () => {
    const schema = intentSchema(PROFILE);
    const properties = schema.properties as Record<string, Record<string, unknown>>;
    expect(properties.genre).toEqual({ type: 'string', enum: PROFILE.genres });
    expect(properties.bpm_min.minimum).toBe(85);
    expect(properties.bpm_max.maximum).toBe(172);
    expect(properties.duration_min.minimum).toBe(45);
    expect(properties.duration_max.maximum).toBe(900);
    expect(properties.energy_min.minimum).toBe(12);
    expect(properties.energy_max.maximum).toBe(98);
    expect(properties.valence_min.minimum).toBe(5);
    expect(properties.arousal_max.maximum).toBe(97);
    expect(properties.mood).toEqual({
      type: 'array',
      items: { type: 'string', enum: PROFILE.moods },
    });
    expect(Object.keys(schema.properties).at(-1)).toBe('query');
  });

  it('is the default schema without a profile', () => {
    expect(intentSchema(null)).toBe(INTENT_SCHEMA);
  });
});

describe('intentExamples', () => {
  it('is the static examples without a profile', () => {
    expect(intentExamples(null)).toBe(INTENT_EXAMPLES);
  });

  it('uses the library’s own numbers and a genre it has', () => {
    const examples = intentExamples(PROFILE);
    expect(examples[0][1]).toContain('"energy_max":40');
    expect(examples[1][0]).toBe('two hours of fast techno');
    expect(examples[1][1]).toContain('"bpm_min":131');
    expect(examples[3][1]).toContain('"danceability_min":72');
    expect(examples[6][1]).toContain('"duration_max":180');
    expect(examples[7][1]).toContain('"duration_min":420');
    for (const [, answer] of examples) {
      if (answer === '{}') continue;
      const parsed = parseIntent(answer);
      if ('error' in parsed) throw new Error(`example does not parse: ${parsed.error}`);
    }
  });
});

describe('INTENT_SCHEMA', () => {
  it('offers exactly the moods and sorts the parser keeps', () => {
    expect([...INTENT_SCHEMA.properties.mood.items.enum]).toEqual([...MOOD_VALUES]);
    expect([...INTENT_SCHEMA.properties.sort.enum]).toEqual([...SORT_VALUES]);
  });

  it('closes the object, requires none of it, and puts free text last', () => {
    expect(INTENT_SCHEMA.additionalProperties).toBe(false);
    expect(INTENT_SCHEMA.required).toEqual([]);
    expect(Object.keys(INTENT_SCHEMA.properties).at(-1)).toBe('query');
    expect(Object.keys(INTENT_SCHEMA.properties).at(-2)).toBe('genre');
  });

  it('states the ranges the parser clamps to', () => {
    expect(INTENT_SCHEMA.properties.bpm_min.minimum).toBe(BPM_RANGE.min);
    expect(INTENT_SCHEMA.properties.bpm_min.maximum).toBe(BPM_RANGE.max);
    expect(INTENT_SCHEMA.properties.duration_min.minimum).toBe(DURATION_RANGE.min);
    expect(INTENT_SCHEMA.properties.duration_max.maximum).toBe(DURATION_RANGE.max);
    expect(INTENT_SCHEMA.properties.limit.maximum).toBe(100);
    expect(INTENT_SCHEMA.properties.offset.maximum).toBe(199);
  });
});
