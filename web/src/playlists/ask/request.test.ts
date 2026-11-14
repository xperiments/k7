/**
 * The shipped path: one sentence, one model call, and the one repair each failure earns.
 *
 * The model is Chrome's and this suite has none; every test here replaces the one call it makes.
 */

import { beforeEach, describe, expect, it, vi } from 'vitest';

import { requestIntent, type IntentTrace } from './request';

const model = vi.hoisted(() => ({ askModel: vi.fn() }));

vi.mock('../../builtin-model', () => ({
  askModel: model.askModel,
  assistantFailure: () => 'The model did not answer.',
}));

describe('the trace sink', () => {
  beforeEach(() => {
    model.askModel.mockReset();
  });

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
  beforeEach(() => {
    model.askModel.mockReset();
  });

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
  beforeEach(() => {
    model.askModel.mockReset();
  });

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
  beforeEach(() => {
    model.askModel.mockReset();
  });

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

describe('the App’s answer ceiling', () => {
  beforeEach(() => {
    model.askModel.mockReset();
  });

  it('returns the model-stopped failure when the signal aborts', async () => {
    model.askModel.mockImplementation(({ signal }: { signal?: AbortSignal }) => {
      if (signal?.aborted) return Promise.reject(new DOMException('Aborted', 'AbortError'));
      return new Promise((_, reject) => {
        signal?.addEventListener('abort', () =>
          reject(new DOMException('Aborted', 'AbortError')),
        );
      });
    });
    const result = await requestIntent('slow', null, undefined, AbortSignal.abort());
    expect(result).toEqual({ error: 'The model did not answer.' });
  });
});

describe('the sentence overrides the model', () => {
  beforeEach(() => {
    model.askModel.mockReset();
  });

  it('gives "10 songs for a workout" the count the sentence states', async () => {
    model.askModel.mockResolvedValue('{"limit":50,"energy_min":70,"arousal_min":60}');
    await expect(requestIntent('10 songs for a workout')).resolves.toEqual({
      limit: 10,
      energyMin: 70,
      arousalMin: 60,
    });
  });

  it('clears the invented side of a stated maximum', async () => {
    model.askModel.mockResolvedValue('{"bpm_min":60,"bpm_max":140}');
    await expect(requestIntent('tracks under 100 BPM')).resolves.toEqual({ bpmMax: 100 });
  });

  it('takes the key from the sentence', async () => {
    model.askModel.mockResolvedValue('{"key":["Am"]}');
    await expect(requestIntent('tracks in 8A or 9A')).resolves.toEqual({ key: ['8A', '9A'] });
  });

  it('reads a listening session as a count', async () => {
    model.askModel.mockResolvedValue('{"genre":"ambient","limit":10}');
    await expect(requestIntent('three hours of ambient')).resolves.toEqual({
      genre: 'ambient',
      limit: 45,
    });
  });

  it('clears both sides of a stated track length', async () => {
    model.askModel.mockResolvedValue('{"duration_min":60,"duration_max":600}');
    await expect(requestIntent('tracks under three minutes')).resolves.toEqual({
      durationMax: 180,
    });
  });

  it('does not turn "add 5 songs to the playlist" into a search', async () => {
    model.askModel.mockResolvedValue('{}');
    const result = await requestIntent('add 5 songs to the playlist');
    expect('error' in result).toBe(true);
    if ('error' in result) expect(result.error).toMatch(/no filters/);
  });
});
