import { describe, expect, it } from 'vitest';
import { parseIntent } from './ask';
import type { SearchArgs } from '../device/mcp';

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

  it('clamps every number to the range the device documents', () => {
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
    expect(args.bpmMin).toBe(0);
    expect(args.bpmMax).toBe(400);
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

  it('trims and caps a free-text query', () => {
    expect(argsOf(parseIntent(JSON.stringify({ query: '  late night  ' }))).query).toBe(
      'late night',
    );
    const long = argsOf(parseIntent(JSON.stringify({ query: 'x'.repeat(300) })));
    expect(long.query).toHaveLength(120);
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
});
