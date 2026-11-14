import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
  BPM_RANGE,
  buildSystemPrompt,
  DURATION_RANGE,
  clampBpm,
  INTENT_EXAMPLES,
  INTENT_SCHEMA,
  intentExamples,
  intentSchema,
  isCategoryQuery,
  isNameQuery,
  MOOD_VALUES,
  normalizeKeys,
  normalizePurpose,
  parseIntent,
  purposeFilters,
  relaxRanges,
  requestIntent,
  SORT_VALUES,
  type IntentTrace,
} from './ask';
import type { SearchArgs } from '../device/mcp';
import type { LibraryProfile } from '../domain/snapshot';

/* The model is Chrome's and this suite has none; the trace tests replace the one call it makes. */
const model = vi.hoisted(() => ({ askModel: vi.fn() }));

vi.mock('../builtin-model', () => ({
  askModel: model.askModel,
  assistantFailure: () => 'The model did not answer.',
}));

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
          duration_min: 60,
          duration_max: 300,
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
      durationMin: 60,
      durationMax: 300,
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

  it('reads a track length in seconds and drops a bound of zero', () => {
    expect(argsOf(parseIntent('{"duration_max":180}'))).toEqual({ durationMax: 180 });
    expect(argsOf(parseIntent('{"duration_min":60,"duration_max":0}'))).toEqual({
      durationMin: 60,
    });
    expect(argsOf(parseIntent('{"duration_min":0,"mood":["happy"]}'))).toEqual({
      mood: ['happy'],
    });
  });

  it('clamps every number to the range the library stores', () => {
    const args = argsOf(
      parseIntent(
        JSON.stringify({
          bpm_min: -10,
          bpm_max: 500,
          duration_max: 900000,
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
    expect(args.durationMax).toBe(DURATION_RANGE.max);
    expect(args.durationMin).toBeUndefined();
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
    expect(argsOf(parseIntent('{"duration_min":240,"duration_max":60}'))).toEqual({
      durationMin: 60,
      durationMax: 240,
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

describe('relaxRanges', () => {
  it('drops the numeric ranges only when a mood is left to search on', () => {
    expect(
      relaxRanges({
        query: 'rain',
        mood: ['relaxed'],
        bpmMax: 100,
        energyMax: 40,
        durationMax: 300,
        limit: 20,
      }),
    ).toEqual({ query: 'rain', mood: ['relaxed'], limit: 20 });
    expect(relaxRanges({ energyMax: 40 })).toBeNull();
    expect(relaxRanges({ mood: ['relaxed'] })).toBeNull();
  });

  it('relaxes a range-heavy ask with the purpose pair, and leaves a hand-run Intent alone', () => {
    const overfill = {
      bpmMin: 130,
      bpmMax: 160,
      energyMin: 70,
      energyMax: 90,
      danceabilityMin: 70,
      limit: 10,
    };
    expect(relaxRanges(overfill)).toBeNull();
    expect(relaxRanges(overfill, {})).toEqual({ limit: 10 });
    expect(relaxRanges(overfill, { purpose: { mood: ['relaxed'], energyMax: 40 } })).toEqual({
      limit: 10,
      energyMax: 40,
    });
    expect(relaxRanges({ bpmMin: 130, energyMin: 70 }, {})).toBeNull();
    expect(relaxRanges({ mood: ['relaxed'], bpmMax: 95 }, { purpose: { energyMax: 40 } })).toEqual({
      mood: ['relaxed'],
    });
  });
});

describe('purposeFilters', () => {
  it('reads the table at the profile, in the five languages', () => {
    expect(purposeFilters('10 songs for a workout', null)).toEqual({
      energyMin: 70,
      arousalMin: 60,
    });
    expect(purposeFilters('something for the gym', null)).toEqual({
      energyMin: 70,
      arousalMin: 60,
    });
    expect(purposeFilters('party playlist', null)).toEqual({
      mood: ['party'],
      danceabilityMin: 70,
    });
    expect(purposeFilters('de la musique calme pour lire', null)).toEqual({
      mood: ['relaxed'],
      energyMax: 40,
    });
    expect(purposeFilters('música triste', null)).toBeNull();
    expect(purposeFilters('sad and slow', null)).toBeNull();
    expect(purposeFilters('slow', null)).toBeNull();
    expect(purposeFilters('workout', PROFILE)).toEqual({ energyMin: 74, arousalMin: 73 });
  });
});

describe('normalizeKeys', () => {
  it('writes a spelled note the device’s way', () => {
    expect(normalizeKeys('B flat major', { key: ['B flat major'] })).toEqual({ key: ['Bb'] });
    expect(normalizeKeys('B flat minor', { key: ['B flat minor'] })).toEqual({ key: ['Bbm'] });
    expect(normalizeKeys('B flat major', { key: ['Bflat'] })).toEqual({ key: ['Bb'] });
    expect(normalizeKeys('B flat major', { key: ['B♭'] })).toEqual({ key: ['Bb'] });
    expect(normalizeKeys('B flat minor', { key: ['B♭m'] })).toEqual({ key: ['Bbm'] });
    expect(normalizeKeys('F sharp minor', { key: ['F sharp minor'] })).toEqual({ key: ['F#m'] });
    expect(normalizeKeys('F sharp minor', { key: ['F♯m'] })).toEqual({ key: ['F#m'] });
    expect(normalizeKeys('B flat major', { key: ['B-flat'] })).toEqual({ key: ['Bb'] });
  });

  it('lets the sentence settle a bare note', () => {
    expect(normalizeKeys('something in F# minor', { key: ['F#'] })).toEqual({ key: ['F#m'] });
    expect(normalizeKeys('something in F# major', { key: ['F#'] })).toEqual({ key: ['F#'] });
    expect(normalizeKeys('something in F#', { key: ['F#'] })).toEqual({ key: ['F#'] });
  });

  it('leaves a key it cannot read, or one the device already reads', () => {
    expect(normalizeKeys('anything', { key: ['8A'] })).toEqual({ key: ['8A'] });
    expect(normalizeKeys('anything', { key: ['E minor'] })).toEqual({ key: ['E minor'] });
    expect(normalizeKeys('H minor', { key: ['H minor'] })).toEqual({ key: ['H minor'] });
    expect(normalizeKeys('B flat major', { key: ['Bb major'] })).toEqual({ key: ['Bb major'] });
  });

  it('keeps an explicit mode over the sentence’s', () => {
    expect(normalizeKeys('B flat minor', { key: ['B flat major'] })).toEqual({ key: ['Bb'] });
  });

  it('dedupes two spellings of one key', () => {
    expect(normalizeKeys('F# minor', { key: ['F#m', 'F♯m'] })).toEqual({ key: ['F#m'] });
  });

  it('returns the same Intent when there is no key', () => {
    const intent: SearchArgs = { bpmMax: 95 };
    expect(normalizeKeys('slow', intent)).toBe(intent);
  });
});

describe('normalizePurpose', () => {
  const heavy: SearchArgs = {
    bpmMin: 130,
    bpmMax: 160,
    energyMin: 70,
    energyMax: 90,
    danceabilityMin: 70,
    danceabilityMax: 90,
    valenceMin: 60,
    valenceMax: 80,
    arousalMin: 60,
    arousalMax: 80,
    sort: 'bpm',
  };

  it('turns the five-pair over-fill into the table answer', () => {
    expect(normalizePurpose('10 songs for a workout', { ...heavy, limit: 10 })).toEqual({
      limit: 10,
      energyMin: 70,
      arousalMin: 60,
    });
    expect(normalizePurpose('something for the gym', heavy)).toEqual({
      energyMin: 70,
      arousalMin: 60,
    });
    expect(normalizePurpose('party playlist', heavy)).toEqual({ danceabilityMin: 70 });
    expect(normalizePurpose('music for sleeping', heavy)).toEqual({ energyMax: 90 });
  });

  it('keeps an explicit tempo, in every language the corpus uses', () => {
    const cases: [string, Partial<SearchArgs>][] = [
      ['fast techno for a workout', { genre: 'techno' }],
      ['rápido para correr', {}],
      ['rapide pour courir', {}],
      ['schnell für das Training', {}],
      ['速いジョギング', {}],
    ];
    for (const [sentence, extra] of cases) {
      expect(normalizePurpose(sentence, { ...heavy, ...extra }).bpmMin, sentence).toBe(130);
    }
  });

  it('drops the tempo a purpose sentence did not state, in every language', () => {
    const cases = [
      'música para estudiar',
      'de la musique pour lire',
      'Musik zum Lernen',
      '勉強のための音楽',
      'workout',
    ];
    for (const sentence of cases) {
      const out = normalizePurpose(sentence, heavy);
      expect(out.bpmMin, sentence).toBeUndefined();
      expect(out.bpmMax, sentence).toBeUndefined();
    }
  });

  it('keeps an explicit valence and an explicit danceability', () => {
    expect(
      normalizePurpose('música alegre para bailar', { ...heavy, mood: ['happy'] }),
    ).toEqual({
      mood: ['happy'],
      valenceMin: 60,
      valenceMax: 80,
      danceabilityMin: 70,
      danceabilityMax: 90,
    });
    expect(normalizePurpose('happy workout', { ...heavy, mood: ['happy'] })).toEqual({
      mood: ['happy'],
      valenceMin: 60,
      valenceMax: 80,
      energyMin: 70,
      arousalMin: 60,
    });
  });

  it('leaves a sentence with no purpose word alone', () => {
    for (const sentence of ['sad and slow', 'música triste', 'fast aggressive techno']) {
      expect(normalizePurpose(sentence, heavy)).toEqual(heavy);
    }
  });

  it('keeps a sort the sentence asked for and drops one it did not', () => {
    expect(normalizePurpose('workout sorted by bpm', heavy).sort).toBe('bpm');
    expect(normalizePurpose('workout', heavy).sort).toBeUndefined();
  });

  it('never turns an Intent that was all invention into an empty one', () => {
    const invented: SearchArgs = { sort: 'bpm', bpmMax: 95 };
    expect(normalizePurpose('workout', invented)).toEqual(invented);
  });

  it('leaves duration alone: it is the track’s length, not the purpose’s', () => {
    const out = normalizePurpose('workout', { ...heavy, durationMax: 180 });
    expect(out.durationMax).toBe(180);
  });
});

const PROFILE: LibraryProfile = {
  tracks: 4120,
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

  it('treats like X and similar to X as the name the prompt says they are', () => {
    expect(isNameQuery('music like Radiohead', 'Radiohead')).toBe(true);
    expect(isNameQuery('sounds like Bonobo', 'bonobo')).toBe(true);
    expect(isNameQuery('something like Bonobo but faster', 'Bonobo')).toBe(true);
    expect(isNameQuery('music similar to Bonobo', 'Bonobo')).toBe(true);
    expect(isNameQuery('similar to the Beatles', 'the Beatles')).toBe(true);
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

describe('the repair flag', () => {
  it('marks an unreadable answer as retryable and an empty Intent as final', () => {
    const unreadable = parseIntent('no json here');
    expect('error' in unreadable && unreadable.retryable).toBe(true);

    const empty = parseIntent('{}');
    expect('error' in empty && empty.retryable).toBeFalsy();
  });
});

describe('the trace sink', () => {
  beforeEach(() => model.askModel.mockReset());

  it('answers identically with and without a trace', async () => {
    model.askModel.mockResolvedValue('{"bpm_max":95}');
    const plain = await requestIntent('slow');
    model.askModel.mockResolvedValue('{"bpm_max":95}');
    const traced = await requestIntent('slow', null, () => {});
    expect(traced).toEqual(plain);
    expect(traced).toEqual({ bpmMax: 95 });
  });

  it('reports one event per model call, and nothing when not passed', async () => {
    model.askModel.mockResolvedValue('{"bpm_max":95}');
    const seen: IntentTrace[] = [];
    const result = await requestIntent('slow', null, (attempt) => seen.push(attempt));
    expect(result).toEqual({ bpmMax: 95 });
    expect(seen).toHaveLength(1);
    expect(seen[0]).toMatchObject({ call: 1, reason: 'first', answer: '{"bpm_max":95}' });
    expect(seen[0].ms).toBeGreaterThanOrEqual(0);
    expect(model.askModel).toHaveBeenCalledTimes(1);

    model.askModel.mockResolvedValue('{"bpm_max":95}');
    await requestIntent('slow');
    expect(model.askModel).toHaveBeenCalledTimes(2);
  });

  it('reports the retry that follows an answer that is not an object', async () => {
    model.askModel
      .mockResolvedValueOnce('I cannot answer that')
      .mockResolvedValueOnce('{"bpm_max":95}');
    const seen: IntentTrace[] = [];
    const result = await requestIntent('slow', null, (attempt) => seen.push(attempt));
    expect(result).toEqual({ bpmMax: 95 });
    expect(seen.map((attempt) => [attempt.call, attempt.reason])).toEqual([
      [1, 'first'],
      [2, 'notJson'],
    ]);
    expect(seen[1].answer).toBe('{"bpm_max":95}');
  });

  it('reports the retry that follows a query that is not a name', async () => {
    model.askModel
      .mockResolvedValueOnce('{"query":"songs"}')
      .mockResolvedValueOnce('{"query":"Daft Punk"}');
    const seen: IntentTrace[] = [];
    const result = await requestIntent('songs by Daft Punk', null, (attempt) => seen.push(attempt));
    expect(result).toEqual({ query: 'Daft Punk' });
    expect(seen.map((attempt) => [attempt.call, attempt.reason])).toEqual([
      [1, 'first'],
      [2, 'queryNotName'],
    ]);
  });

  it('passes the abort signal to every model call it makes', async () => {
    model.askModel.mockResolvedValue('{"bpm_max":95}');
    const controller = new AbortController();
    await requestIntent('slow', null, undefined, controller.signal);
    const [options] = model.askModel.mock.calls[0] as [{ signal?: AbortSignal }];
    expect(options.signal).toBe(controller.signal);
  });

  it('does not call the model, or the sink, for an empty sentence', async () => {
    const result = await requestIntent('   ', null, () => {
      throw new Error('the sink must not fire without a model call');
    });
    expect(result).toEqual({ error: 'Say what you are looking for first.' });
    expect(model.askModel).not.toHaveBeenCalled();
  });
});

describe('the purpose normalizer on the shipped path', () => {
  beforeEach(() => model.askModel.mockReset());

  const overfill = JSON.stringify({
    bpm_min: 130,
    bpm_max: 160,
    energy_min: 70,
    energy_max: 90,
    danceability_min: 70,
    danceability_max: 90,
    valence_min: 60,
    valence_max: 80,
    arousal_min: 60,
    arousal_max: 80,
    sort: 'bpm',
    limit: 10,
  });

  it('returns the table answer for the over-fill', async () => {
    model.askModel.mockResolvedValue(overfill);
    await expect(requestIntent('10 songs for a workout')).resolves.toEqual({
      limit: 10,
      energyMin: 70,
      arousalMin: 60,
    });
  });

  it('leaves a sentence with no purpose word alone', async () => {
    model.askModel.mockResolvedValue('{"bpm_max":95}');
    await expect(requestIntent('slow')).resolves.toEqual({ bpmMax: 95 });
  });

  it('normalizes a repaired answer, after the repair', async () => {
    model.askModel
      .mockResolvedValueOnce('{"query":"songs","bpm_min":130}')
      .mockResolvedValueOnce('{"query":"Daft Punk","bpm_min":130}');
    await expect(requestIntent('workout songs by Daft Punk')).resolves.toEqual({
      query: 'Daft Punk',
    });
  });

  it('still traces the raw answer, before normalizing', async () => {
    model.askModel.mockResolvedValue(overfill);
    const seen: IntentTrace[] = [];
    await expect(requestIntent('10 songs for a workout', null, (a) => seen.push(a))).resolves.toEqual({
      limit: 10,
      energyMin: 70,
      arousalMin: 60,
    });
    expect(seen[0].answer).toBe(overfill);
  });
});

describe('the like/similar fix on the shipped path', () => {
  beforeEach(() => model.askModel.mockReset());

  it('keeps an artist query the repair used to drop, with one call', async () => {
    model.askModel.mockResolvedValue('{"query":"Radiohead"}');
    await expect(requestIntent('music like Radiohead')).resolves.toEqual({ query: 'Radiohead' });
    expect(model.askModel).toHaveBeenCalledTimes(1);

    model.askModel.mockResolvedValue('{"query":"Bonobo"}');
    await expect(requestIntent('similar to Bonobo')).resolves.toEqual({ query: 'Bonobo' });
    expect(model.askModel).toHaveBeenCalledTimes(2);
  });
});

describe('key spellings on the shipped path', () => {
  beforeEach(() => model.askModel.mockReset());

  it('rewrites a spelled key before returning the Intent', async () => {
    model.askModel.mockResolvedValue('{"key":["B flat major"]}');
    await expect(requestIntent('B flat major')).resolves.toEqual({ key: ['Bb'] });
  });

  it('lets the sentence settle a bare note', async () => {
    model.askModel.mockResolvedValue('{"key":["F#"]}');
    await expect(requestIntent('something in F# minor')).resolves.toEqual({ key: ['F#m'] });
  });

  it('leaves a valid key alone', async () => {
    model.askModel.mockResolvedValue('{"key":["Am"]}');
    await expect(requestIntent('something in A minor')).resolves.toEqual({ key: ['Am'] });
  });
});
