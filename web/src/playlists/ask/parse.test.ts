/**
 * The Intent is untrusted text from a model, so these tests are about the boundary: what
 * survives, what is clamped, and what is refused outright. No network is involved.
 */

import { describe, expect, it } from 'vitest';

import { BPM_RANGE, clampBpm, DURATION_RANGE } from './contract';
import { isCategoryQuery, isNameQuery, parseIntent } from './parse';
import { argsOf, errorOf } from './test-support';

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

  it('reads a name in the five languages', () => {
    expect(isNameQuery('canciones de bonobo', 'bonobo')).toBe(true);
    expect(isNameQuery('chansons de bonobo', 'bonobo')).toBe(true);
    expect(isNameQuery('Lieder von bonobo', 'bonobo')).toBe(true);
    expect(isNameQuery('canciones como bonobo', 'bonobo')).toBe(true);
    expect(isNameQuery('chansons comme bonobo', 'bonobo')).toBe(true);
    expect(isNameQuery('Lieder wie bonobo', 'bonobo')).toBe(true);
    expect(isNameQuery('canciones parecido a bonobo', 'bonobo')).toBe(true);
    expect(isNameQuery('ボノボの曲', 'ボノボ')).toBe(true);
    expect(isNameQuery('ボノボによる曲', 'ボノボ')).toBe(true);
  });

  it('does not mistake a partitive for a name', () => {
    expect(isNameQuery('de la musique', 'musique')).toBe(false);
    expect(isNameQuery('de la musique', 'la musique')).toBe(false);
    expect(isNameQuery('von der Musik', 'Musik')).toBe(false);
    expect(isNameQuery('chansons de la banda', 'banda')).toBe(false);
  });

  it('refuses a name after a context marker in the five languages', () => {
    expect(isNameQuery('música para bonobo', 'bonobo')).toBe(false);
    expect(isNameQuery('musique pour bonobo', 'bonobo')).toBe(false);
    expect(isNameQuery('Musik für bonobo', 'bonobo')).toBe(false);
    expect(isNameQuery('ボノボのために', 'ボノボ')).toBe(false);
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
