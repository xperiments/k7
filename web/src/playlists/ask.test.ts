import { describe, expect, it } from 'vitest';
import {
  BPM_RANGE,
  buildSystemPrompt,
  clampBpm,
  INTENT_EXAMPLES,
  INTENT_SCHEMA,
  intentExamples,
  intentSchema,
  isCategoryQuery,
  isNameQuery,
  MOOD_VALUES,
  parseIntent,
  relaxRanges,
  SORT_VALUES,
} from './ask';
import type { SearchArgs } from '../device/mcp';
import type { LibraryProfile } from '../domain/snapshot';

/**
 * The Intent is untrusted text from a model, so the tests are about the boundary: what
 * survives, what is clamped, and what is refused outright. No network is involved.
 */
function argsOf(result: SearchArgs | { error: string }): SearchArgs {
  if ('error' in result) throw new Error(`expected filters, got: ${result.error}`);
  return result;
}

function errorOf(result: SearchArgs | { error: string }): string {
  if (!('error' in result)) throw new Error(`expected a failure, got: ${JSON.stringify(result)}`);
  return result.error;
}

describe('parseIntent', () => {
  it('reads the tool arguments the model was asked for', () => {
    const args = argsOf(
      parseIntent(
        JSON.stringify({
          query: 'rainy sunday',
          genre: 'ambient',
          bpm_min: 118,
          bpm_max: 124,
          energy_min: 40,
          energy_max: 70,
          danceability_min: 30,
          danceability_max: 60,
          valence_min: 20,
          valence_max: 80,
          arousal_min: 10,
          arousal_max: 90,
          key: ['8A', '9A'],
          mood: ['relaxed'],
          sort: 'bpm',
          offset: 0,
          limit: 20,
        }),
      ),
    );

    expect(args).toEqual({
      query: 'rainy sunday',
      genre: 'ambient',
      bpmMin: 118,
      bpmMax: 124,
      energyMin: 40,
      energyMax: 70,
      danceabilityMin: 30,
      danceabilityMax: 60,
      valenceMin: 20,
      valenceMax: 80,
      arousalMin: 10,
      arousalMax: 90,
      key: ['8A', '9A'],
      mood: ['relaxed'],
      sort: 'bpm',
      offset: 0,
      limit: 20,
    });
  });

  it('tolerates a code fence and a sentence around the object', () => {
    const fenced = argsOf(parseIntent('```json\n{"bpm_max": 126}\n```'));
    expect(fenced).toEqual({ bpmMax: 126 });

    const prose = argsOf(parseIntent('Here are the filters: {"mood":["party"]} — enjoy.'));
    expect(prose).toEqual({ mood: ['party'] });
  });

  it('drops keys that are not library_search arguments', () => {
    const args = argsOf(
      parseIntent(
        JSON.stringify({ bpm_min: 120, track: 'Blue Monday', artist: 'New Order', tracks: [] }),
      ),
    );
    expect(args).toEqual({ bpmMin: 120 });
  });

  it('clamps every number to the range the library stores', () => {
    const args = argsOf(
      parseIntent(
        JSON.stringify({
          bpm_min: -10,
          bpm_max: 500,
          energy_min: -5,
          energy_max: 150,
          valence_max: 101,
          arousal_min: -0.4,
          limit: 500,
          offset: 500,
        }),
      ),
    );
    /* Tempo is stored folded into 70-180, so anything outside it could never match. */
    expect(args.bpmMin).toBe(70);
    expect(args.bpmMax).toBe(180);
    expect(args.energyMin).toBe(0);
    expect(args.energyMax).toBe(100);
    expect(args.valenceMax).toBe(100);
    expect(args.arousalMin).toBe(0);
    expect(args.offset).toBe(199);
    // 199 + 100 is refused by the device; the page is trimmed instead.
    expect(args.limit).toBe(1);
  });

  it('trims a page that would run past the device limit of 200 rows', () => {
    expect(argsOf(parseIntent('{"offset":190,"limit":100}'))).toEqual({ offset: 190, limit: 10 });
    expect(argsOf(parseIntent('{"offset":150}'))).toEqual({ offset: 150, limit: 50 });
  });

  it('keeps a limit on its own, and refuses one below one row', () => {
    expect(argsOf(parseIntent('{"limit":25}'))).toEqual({ limit: 25 });
    expect(argsOf(parseIntent('{"limit":0}'))).toEqual({ limit: 1 });
  });

  it('keeps only the five moods, lowercased and without repeats', () => {
    const args = argsOf(
      parseIntent(JSON.stringify({ mood: ['Happy', 'happy', 'angry', 'relaxed', 'DANCEABLE'] })),
    );
    expect(args.mood).toEqual(['happy', 'relaxed']);
  });

  it('keeps only the five sorts', () => {
    expect(argsOf(parseIntent('{"sort":"BPM"}')).sort).toBe('bpm');
    expect(argsOf(parseIntent('{"sort":"bpm"}')).sort).toBe('bpm');
    expect(argsOf(parseIntent('{"query":"x","sort":"loudest"}')).sort).toBeUndefined();
  });

  it('keeps key strings as they were written, and drops the rest', () => {
    const args = argsOf(parseIntent(JSON.stringify({ key: ['8A', 'Am', '  ', 8, null, '8A'] })));
    expect(args.key).toEqual(['8A', 'Am']);
  });

  it('refuses numbers sent as strings, because the device would too', () => {
    const error = errorOf(parseIntent(JSON.stringify({ bpm_min: '120', limit: '20' })));
    expect(error).toMatch(/no filters/);
  });

  it('drops words to find that only restate a key', () => {
    expect(argsOf(parseIntent('{"query":"B minor","key":["Bm"]}'))).toEqual({ key: ['Bm'] });
    expect(argsOf(parseIntent('{"query":"B♭ minor","key":["Bbm"]}'))).toEqual({ key: ['Bbm'] });
    expect(argsOf(parseIntent('{"query":"8A","key":["8A"]}'))).toEqual({ key: ['8A'] });
    /* A title that happens to start with the key is not the key. */
    expect(argsOf(parseIntent('{"query":"B minor blues","key":["Bm"]}'))).toEqual({
      key: ['Bm'],
      query: 'B minor blues',
    });
    /* With no key, the words are all the filter there is. */
    expect(argsOf(parseIntent('{"query":"B minor"}'))).toEqual({ query: 'B minor' });
  });

  it('trims and caps a free-text query at the device byte ceiling', () => {
    expect(argsOf(parseIntent(JSON.stringify({ query: '  late night  ' }))).query).toBe(
      'late night',
    );
    const long = argsOf(parseIntent(JSON.stringify({ query: 'x'.repeat(300) })));
    expect(long.query).toHaveLength(255);
  });

  it('caps a query and a genre by bytes, not characters', () => {
    const query = argsOf(parseIntent(JSON.stringify({ query: 'é'.repeat(200) })));
    expect(new TextEncoder().encode(query.query ?? '').length).toBeLessThanOrEqual(255);

    const genre = argsOf(parseIntent(JSON.stringify({ genre: 'é'.repeat(100) })));
    expect(new TextEncoder().encode(genre.genre ?? '').length).toBeLessThanOrEqual(63);
    /* The cut lands on a code-point boundary: no replacement character. */
    expect(genre.genre).not.toContain('\ufffd');
  });

  it('refuses an answer that carries no usable filter', () => {
    expect(errorOf(parseIntent('{}'))).toMatch(/no filters/);
    expect(errorOf(parseIntent('{"track":"Blue Monday"}'))).toMatch(/no filters/);
    expect(errorOf(parseIntent('{"bpm_min":"fast"}'))).toMatch(/no filters/);
  });

  it('refuses anything that is not a JSON object', () => {
    expect(errorOf(parseIntent(''))).toMatch(/nothing/);
    expect(errorOf(parseIntent('I think you would like some jazz.'))).toMatch(/not JSON/);
    expect(errorOf(parseIntent('["8A"]'))).toMatch(/not a JSON object/);
    expect(errorOf(parseIntent('null'))).toMatch(/not a JSON object/);
  });

  it('swaps an inverted pair instead of keeping an impossible range', () => {
    expect(argsOf(parseIntent('{"bpm_min":140,"bpm_max":120}'))).toEqual({
      bpmMin: 120,
      bpmMax: 140,
    });
    expect(argsOf(parseIntent('{"energy_min":90,"energy_max":20}'))).toEqual({
      energyMin: 20,
      energyMax: 90,
    });
    expect(argsOf(parseIntent('{"danceability_min":80,"danceability_max":10}'))).toEqual({
      danceabilityMin: 10,
      danceabilityMax: 80,
    });
    expect(argsOf(parseIntent('{"valence_min":70,"valence_max":30}'))).toEqual({
      valenceMin: 30,
      valenceMax: 70,
    });
    expect(argsOf(parseIntent('{"arousal_min":95,"arousal_max":5}'))).toEqual({
      arousalMin: 5,
      arousalMax: 95,
    });
  });
});

describe('clampBpm', () => {
  it('keeps a hand-typed tempo inside the folded range', () => {
    expect(BPM_RANGE).toEqual({ min: 70, max: 180 });
    expect(clampBpm(60)).toBe(70);
    expect(clampBpm(200)).toBe(180);
    expect(clampBpm(120)).toBe(120);
  });
});

describe('isCategoryQuery', () => {
  it('knows a bare category from a name', () => {
    expect(isCategoryQuery('songs')).toBe(true);
    expect(isCategoryQuery('  Songs. ')).toBe(true);
    expect(isCategoryQuery('the music')).toBe(true);
    expect(isCategoryQuery('party songs')).toBe(false);
    expect(isCategoryQuery('Daft Punk')).toBe(false);
    expect(isCategoryQuery(undefined)).toBe(false);
  });
});

describe('INTENT_EXAMPLES', () => {
  it('every example answers with an Intent parseIntent accepts', () => {
    const expected: Record<string, SearchArgs | null> = {
      'twenty relaxed tracks for sleeping': { mood: ['relaxed'], energyMax: 35, limit: 20 },
      'two hours of fast techno': { genre: 'techno', bpmMin: 125, limit: 30 },
      'songs by Daft Punk': { query: 'Daft Punk' },
      'something in B minor, danceable': { key: ['Bm'], danceabilityMin: 65 },
      'música tranquila para estudiar': { mood: ['relaxed'], energyMax: 40 },
      'slowest tracks first': { sort: 'bpm' },
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

describe('relaxRanges', () => {
  it('drops the numeric ranges only when a mood is left to search on', () => {
    expect(
      relaxRanges({ query: 'rain', mood: ['relaxed'], bpmMax: 100, energyMax: 40, limit: 20 }),
    ).toEqual({ query: 'rain', mood: ['relaxed'], limit: 20 });
    expect(relaxRanges({ energyMax: 40 })).toBeNull();
    expect(relaxRanges({ mood: ['relaxed'] })).toBeNull();
  });
});

const PROFILE: LibraryProfile = {
  tracks: 4120,
  genres: ['techno', 'ambient'],
  keys: ['8A', '9B'],
  moods: ['happy', 'relaxed'],
  bpm: { min: 85, p25: 98, p50: 120, p75: 131, max: 172 },
  energy: { min: 12, p25: 40, p50: 60, p75: 74, max: 98 },
  danceability: { min: 10, p25: 35, p50: 55, p75: 72, max: 95 },
  valence: { min: 5, p25: 30, p50: 50, p75: 70, max: 99 },
  arousal: { min: 8, p25: 32, p50: 55, p75: 73, max: 97 },
};

describe('buildSystemPrompt', () => {
  it('uses the library the profile describes', () => {
    const prompt = buildSystemPrompt(PROFILE);
    expect(prompt).toContain('85..172');
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
    for (const [, answer] of examples) {
      if (answer === '{}') continue;
      const parsed = parseIntent(answer);
      if ('error' in parsed) throw new Error(`example does not parse: ${parsed.error}`);
    }
  });
});

describe('isNameQuery', () => {
  it('keeps a name the sentence marks', () => {
    expect(isNameQuery('songs by Daft Punk', 'Daft Punk')).toBe(true);
    expect(isNameQuery('songs by daft punk', 'daft punk')).toBe(true);
    expect(isNameQuery('Daft Punk', 'Daft Punk')).toBe(true);
    expect(isNameQuery('tracks from the album Discovery', 'Discovery')).toBe(true);
    expect(isNameQuery('music called Blue Monday', 'Blue Monday')).toBe(true);
  });

  it('refuses a category, a purpose and an unmarked lowercase word', () => {
    expect(isNameQuery('songs by Daft Punk', 'songs')).toBe(false);
    expect(isNameQuery('10 songs for a workout', 'workout')).toBe(false);
    expect(isNameQuery('10 songs for a workout', 'Workout')).toBe(false);
    expect(isNameQuery('music for reading', 'reading')).toBe(false);
    expect(isNameQuery('a late night', 'late night')).toBe(false);
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
    expect(INTENT_SCHEMA.properties.limit.maximum).toBe(100);
    expect(INTENT_SCHEMA.properties.offset.maximum).toBe(199);
  });
});

describe('the repair flag', () => {
  it('marks an unreadable answer as retryable and an empty Intent as final', () => {
    const unreadable = parseIntent('no json here');
    expect('error' in unreadable && unreadable.retryable).toBe(true);

    const empty = parseIntent('{}');
    expect('error' in empty && empty.retryable).toBeFalsy();
  });
});
