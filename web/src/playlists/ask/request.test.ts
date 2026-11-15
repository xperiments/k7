/**
 * The shipped path: one sentence, one model call, and the one repair each failure earns.
 *
 * The model is Chrome's and this suite has none; every test here replaces the one call it makes.
 */

import { beforeEach, describe, expect, it, vi } from 'vitest';

import { requestIntent, type IntentTrace } from './request';
import { TEST_PROFILE as PROFILE } from './test-support';

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
      .mockResolvedValueOnce('{"query":"late night"}')
      .mockResolvedValueOnce('{"query":"Late Night"}');
    const seen: IntentTrace[] = [];
    const result = await requestIntent('a late night', null, (attempt) => seen.push(attempt));
    expect(result).toEqual({ query: 'Late Night' });
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
      .mockResolvedValueOnce('{"query":"workout"}')
      .mockResolvedValueOnce('{"energy_min":60,"energy_max":90}');
    await expect(requestIntent('workout')).resolves.toEqual({ energyMin: 70 });
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

  it('reads "add 5 songs to the playlist" as the count it states', async () => {
    model.askModel.mockResolvedValue('{}');
    // The App applies the Intent to the list beside the panel, so an instruction to change
    // the list is a search like any other (ADR 0093).
    await expect(requestIntent('add 5 songs to the playlist')).resolves.toEqual({ limit: 5 });
  });
});

describe('the purpose table fills an empty answer', () => {
  beforeEach(() => {
    model.askModel.mockReset();
  });

  it('turns the empty answer for "10 songs for a workout" into the table plus the count', async () => {
    model.askModel.mockResolvedValue('{}');
    await expect(requestIntent('10 songs for a workout')).resolves.toEqual({
      limit: 10,
      energyMin: 70,
      arousalMin: 60,
    });
  });

  it('turns the empty answer for "party playlist" into the table', async () => {
    model.askModel.mockResolvedValue('{}');
    await expect(requestIntent('party playlist')).resolves.toEqual({
      mood: ['party'],
      danceabilityMin: 70,
    });
  });

  it('still refuses when there is no purpose and no stated filter', async () => {
    model.askModel.mockResolvedValue('{}');
    const result = await requestIntent('play something');
    expect('error' in result).toBe(true);
  });
});

describe('the sentence fills an empty answer', () => {
  beforeEach(() => {
    model.askModel.mockReset();
  });

  it('recovers a stated BPM', async () => {
    model.askModel.mockResolvedValue('{}');
    await expect(requestIntent('tracks under 100 BPM')).resolves.toEqual({ bpmMax: 100 });
  });

  it('recovers a stated track length', async () => {
    model.askModel.mockResolvedValue('{}');
    await expect(requestIntent('long mixes')).resolves.toEqual({ durationMin: 480 });
  });

  it('recovers a stated count', async () => {
    model.askModel.mockResolvedValue('{}');
    await expect(requestIntent('50 tracks')).resolves.toEqual({ limit: 50 });
  });

  it('recovers a stated listening session and genre', async () => {
    model.askModel.mockResolvedValue('{}');
    await expect(requestIntent('three hours of ambient')).resolves.toEqual({
      genre: 'ambient',
      limit: 45,
    });
  });

  it('recovers a stated offset and key', async () => {
    model.askModel.mockResolvedValue('{}');
    await expect(requestIntent('skip 40')).resolves.toEqual({ offset: 40 });
    model.askModel.mockResolvedValue('{}');
    await expect(requestIntent('tracks in 8A')).resolves.toEqual({ key: ['8A'] });
  });

  it('still refuses an instruction to change the list that states nothing to search', async () => {
    model.askModel.mockResolvedValue('{}');
    const result = await requestIntent('add songs to the playlist');
    expect('error' in result).toBe(true);
  });
});

describe('a repaired answer still goes through the table', () => {
  beforeEach(() => {
    model.askModel.mockReset();
  });

  it('normalizes the answer after the JSON repair, not before', async () => {
    model.askModel
      .mockResolvedValueOnce('I cannot answer that')
      .mockResolvedValueOnce('{"energy_min":60,"energy_max":90}');
    await expect(requestIntent('workout')).resolves.toEqual({ energyMin: 70 });
  });
});

describe("the purpose table's moods", () => {
  const PARTY = { ...PROFILE, moods: ['happy', 'party', 'relaxed'] };

  beforeEach(() => {
    model.askModel.mockReset();
  });

  it('drops a mood the library does not have, keeping the numeric side', async () => {
    model.askModel.mockResolvedValue('{"mood":["party"],"danceability_min":70}');
    await expect(requestIntent('party playlist', PROFILE)).resolves.toEqual({
      danceabilityMin: 72,
    });
  });

  it('keeps the mood when the library has it', async () => {
    model.askModel.mockResolvedValue('{"mood":["party"],"danceability_min":70}');
    await expect(requestIntent('party playlist', PARTY)).resolves.toEqual({
      mood: ['party'],
      danceabilityMin: 72,
    });
  });

  it('fills the numeric side from an empty answer when the library has no party', async () => {
    model.askModel.mockResolvedValue('{}');
    await expect(requestIntent('party playlist', PROFILE)).resolves.toEqual({
      danceabilityMin: 72,
    });
  });
});

describe('names in the five languages', () => {
  beforeEach(() => {
    model.askModel.mockReset();
  });

  it('keeps a marked name with one model call', async () => {
    const cases: [string, string][] = [
      ['canciones de bonobo', 'bonobo'],
      ['chansons de bonobo', 'bonobo'],
      ['Lieder von bonobo', 'bonobo'],
      ['canciones como bonobo', 'bonobo'],
      ['chansons comme bonobo', 'bonobo'],
      ['Lieder wie bonobo', 'bonobo'],
      ['canciones parecido a bonobo', 'bonobo'],
      ['ボノボの曲', 'ボノボ'],
    ];
    for (const [sentence, query] of cases) {
      model.askModel.mockReset();
      model.askModel.mockResolvedValue(JSON.stringify({ query }));
      await expect(requestIntent(sentence), sentence).resolves.toEqual({ query });
      expect(model.askModel, sentence).toHaveBeenCalledTimes(1);
    }
  });

  it('still repairs a partitive or a name after a context marker', async () => {
    const cases: [string, string][] = [
      ['de la musique', 'musique'],
      ['música para bonobo', 'bonobo'],
      ['Musik für bonobo', 'bonobo'],
      ['ボノボのために', 'ボノボ'],
    ];
    for (const [sentence, query] of cases) {
      model.askModel.mockReset();
      model.askModel
        .mockResolvedValueOnce(JSON.stringify({ query }))
        .mockResolvedValueOnce('{"mood":["relaxed"]}');
      await expect(requestIntent(sentence), sentence).resolves.toEqual({ mood: ['relaxed'] });
      expect(model.askModel, sentence).toHaveBeenCalledTimes(2);
    }
  });
});

describe('the sentence before the repair', () => {
  beforeEach(() => {
    model.askModel.mockReset();
  });

  it('takes the name the sentence marks, with one call', async () => {
    const cases: [string, string, string][] = [
      ['songs by Daft Punk', 'songs', 'Daft Punk'],
      ['music like Radiohead', 'music', 'Radiohead'],
      ['canciones de bonobo', 'canciones', 'bonobo'],
    ];
    for (const [sentence, bad, name] of cases) {
      model.askModel.mockReset();
      model.askModel.mockResolvedValue(JSON.stringify({ query: bad }));
      await expect(requestIntent(sentence), sentence).resolves.toEqual({ query: name });
      expect(model.askModel, sentence).toHaveBeenCalledTimes(1);
    }
  });

  it('drops a bad query when the sentence keeps the count', async () => {
    model.askModel.mockResolvedValue('{"query":"workout"}');
    await expect(requestIntent('10 songs for a workout')).resolves.toEqual({
      limit: 10,
      energyMin: 70,
      arousalMin: 60,
    });
    expect(model.askModel).toHaveBeenCalledTimes(1);
  });

  it('still asks again when the query is the only filter', async () => {
    model.askModel
      .mockResolvedValueOnce('{"query":"songs"}')
      .mockResolvedValueOnce('{"query":"Daft Punk"}');
    await expect(requestIntent('Daft Punk')).resolves.toEqual({ query: 'Daft Punk' });
    expect(model.askModel).toHaveBeenCalledTimes(2);
  });
});

describe('the model’s genre', () => {
  beforeEach(() => {
    model.askModel.mockReset();
  });

  it('is dropped when the sentence never names one', async () => {
    model.askModel.mockResolvedValue('{"genre":"party","bpm_min":120}');
    await expect(requestIntent('tracks for the whole library')).resolves.toEqual({ bpmMin: 120 });
  });

  it('is kept when the sentence names it', async () => {
    model.askModel.mockResolvedValue('{"genre":"techno"}');
    await expect(requestIntent('fast techno')).resolves.toEqual({ genre: 'techno' });
  });

  it('is replaced by the genre the sentence names', async () => {
    model.askModel.mockResolvedValue('{"genre":"techno","bpm_min":120}');
    await expect(requestIntent('ambient')).resolves.toEqual({ genre: 'ambient', bpmMin: 120 });
  });
});
