/**
 * The sentence extractors, without a model: the numbers and keys a sentence states, and the
 * cases that must not be mistaken for one.
 */

import { describe, expect, it } from 'vitest';

import type { SearchArgs } from '../../device/mcp';

import { applySentenceFilters, sentenceFilters } from './sentence';
import { TEST_PROFILE } from './test-support';

describe('counts', () => {
  it('reads a stated number of tracks', () => {
    expect(sentenceFilters('10 songs for a workout')).toEqual({ limit: 10 });
    expect(sentenceFilters('50 tracks')).toEqual({ limit: 50 });
    expect(sentenceFilters('twenty tracks')).toEqual({ limit: 20 });
    expect(sentenceFilters('the 20 most danceable tracks')).toEqual({ limit: 20 });
    expect(sentenceFilters('25 new songs')).toEqual({ limit: 25 });
    expect(sentenceFilters('a hundred tracks')).toEqual({ limit: 100 });
  });

  it('reads a listening session at fifteen tracks an hour', () => {
    expect(sentenceFilters('about an hour of relaxed music')).toEqual({ limit: 15 });
    expect(sentenceFilters('two hours of fast techno')).toEqual({ limit: 30, genre: 'techno' });
    expect(sentenceFilters('three hours of ambient')).toEqual({ limit: 45, genre: 'ambient' });
  });

  it('reads a skip as an offset, and keeps a count beside it', () => {
    expect(sentenceFilters('skip 40')).toEqual({ offset: 40 });
    expect(sentenceFilters('skip 40 and 50 tracks')).toEqual({ offset: 40, limit: 50 });
  });

  it('leaves a sentence with no number alone', () => {
    expect(sentenceFilters('the whole library')).toEqual({});
    expect(sentenceFilters('play something')).toEqual({});
  });

  it('still reads a command; the request path decides whether to apply it', () => {
    expect(sentenceFilters('add 5 songs to the playlist')).toEqual({ limit: 5 });
  });
});

describe('durations', () => {
  it('reads a track length in seconds', () => {
    expect(sentenceFilters('tracks under three minutes')).toEqual({ durationMax: 180 });
    expect(sentenceFilters('tracks under 3 minutes')).toEqual({ durationMax: 180 });
    expect(sentenceFilters('over 8 min')).toEqual({ durationMin: 480 });
    expect(sentenceFilters('at least 30 seconds')).toEqual({ durationMin: 30 });
    expect(sentenceFilters('under 30 seconds')).toEqual({ durationMax: 30 });
    expect(sentenceFilters('long mixes')).toEqual({ durationMin: 480 });
    expect(sentenceFilters('long mixes', TEST_PROFILE)).toEqual({ durationMin: 420 });
    expect(sentenceFilters('short tracks')).toEqual({ durationMax: 180 });
  });

  it('does not mistake a listening session for a track length', () => {
    expect(sentenceFilters('about an hour')).toEqual({ limit: 15 });
  });
});

describe('genres', () => {
  it('reads a genre the sentence names, from the profile or the static list', () => {
    expect(sentenceFilters('three hours of ambient')).toEqual({ genre: 'ambient', limit: 45 });
    expect(sentenceFilters('techno', TEST_PROFILE)).toEqual({ genre: 'techno' });
    expect(sentenceFilters('reggaeton', TEST_PROFILE)).toEqual({ genre: 'reggaeton' });
    expect(sentenceFilters('a house party')).toEqual({});
    expect(sentenceFilters('lo-fi beats')).toEqual({});
    expect(applySentenceFilters({ genre: 'techno' }, { genre: 'ambient' })).toEqual({
      genre: 'ambient',
    });
  });

  it('reads a capitalised genre, and still knows a name when it sees one', () => {
    expect(sentenceFilters('Ambient music for a rainy day')).toEqual({ genre: 'ambient' });
    expect(sentenceFilters('three hours of Ambient')).toEqual({ genre: 'ambient', limit: 45 });
    expect(sentenceFilters('Track 1: Ambient')).toEqual({ genre: 'ambient' });
    expect(sentenceFilters('Play some Techno')).toEqual({ genre: 'techno' });
    expect(sentenceFilters('songs by Daft Punk')).toEqual({});
    expect(sentenceFilters('Listen to Daft Punk')).toEqual({});
    expect(sentenceFilters('the album Discovery')).toEqual({});
  });
});

describe('BPM', () => {
  it('reads a stated pair', () => {
    expect(sentenceFilters('tracks between 85 and 95 BPM')).toEqual({
      bpmMin: 85,
      bpmMax: 95,
    });
    expect(sentenceFilters('140 to 120 BPM')).toEqual({ bpmMin: 120, bpmMax: 140 });
  });

  it('reads one side, and clears the other in the Intent', () => {
    expect(sentenceFilters('tracks under 100 BPM')).toEqual({ bpmMax: 100 });
    expect(sentenceFilters('at least 130 BPM')).toEqual({ bpmMin: 130 });
  });

  it('reads a bare value and a stated window', () => {
    expect(sentenceFilters('120 bpm')).toEqual({ bpmMin: 120, bpmMax: 120 });
    expect(sentenceFilters('around 140 bpm')).toEqual({ bpmMin: 135, bpmMax: 145 });
    expect(sentenceFilters('BPM 120')).toEqual({ bpmMin: 120, bpmMax: 120 });
  });

  it('clamps the folded tempo range', () => {
    expect(sentenceFilters('songs between 300 and 400 BPM')).toEqual({
      bpmMin: 180,
      bpmMax: 180,
    });
  });

  it('leaves a word or a unitless number to the model', () => {
    expect(sentenceFilters('fast aggressive techno')).toEqual({ genre: 'techno' });
    expect(sentenceFilters('around 140')).toEqual({});
  });
});

describe('keys', () => {
  it('reads a key the sentence states', () => {
    expect(sentenceFilters('something in F# minor')).toEqual({ key: ['F# minor'] });
    expect(sentenceFilters('B flat major')).toEqual({ key: ['Bb'] });
    expect(sentenceFilters('tracks in 8A or 9A')).toEqual({ key: ['8A', '9A'] });
    expect(sentenceFilters('E minor to dance to')).toEqual({ key: ['E minor'] });
    expect(sentenceFilters('Camelot 8A')).toEqual({ key: ['8A'] });
    expect(sentenceFilters('the key is A minor')).toEqual({ key: ['A minor'] });
  });

  it('reads a suffix spelling the device can read', () => {
    expect(sentenceFilters('in F♯m')).toEqual({ key: ['F#m'] });
    expect(sentenceFilters('B♭ minor')).toEqual({ key: ['Bbm'] });
  });

  it('does not mistake an article for a key', () => {
    expect(sentenceFilters('make a minor adjustment')).toEqual({});
    expect(sentenceFilters('in a minor key')).toEqual({});
  });

  it('does not mistake a name for a key', () => {
    expect(sentenceFilters('play Blue Monday')).toEqual({});
    expect(sentenceFilters('the album Discovery')).toEqual({});
  });

  it('never returns a key the device cannot read', () => {
    expect(sentenceFilters('13A')).toEqual({});
  });
});

describe('together', () => {
  it('reads the count, the tempo and the key in one sentence', () => {
    expect(sentenceFilters('10 songs between 120 and 130 BPM in F# minor')).toEqual({
      limit: 10,
      bpmMin: 120,
      bpmMax: 130,
      key: ['F# minor'],
    });
  });
});

describe('applySentenceFilters', () => {
  it('replaces a pair rather than keeping the other side', () => {
    expect(
      applySentenceFilters({ bpmMin: 140, bpmMax: 160 }, { bpmMax: 100 }),
    ).toEqual({ bpmMax: 100 });
    expect(applySentenceFilters({ durationMin: 60 }, { durationMax: 180 })).toEqual({
      durationMax: 180,
    });
  });

  it('replaces a key and keeps the other filters', () => {
    expect(applySentenceFilters({ key: ['Am'], query: 'x' }, { key: ['Bm'] })).toEqual({
      key: ['Bm'],
      query: 'x',
    });
  });

  it('keeps the count and the offset inside the device page', () => {
    const intent: SearchArgs = { limit: 100, offset: 190 };
    expect(applySentenceFilters(intent, { limit: 50 })).toEqual({ limit: 10, offset: 190 });
    expect(applySentenceFilters({}, { offset: 40 })).toEqual({ offset: 40 });
  });

  it('leaves an Intent alone when the sentence states nothing', () => {
    expect(applySentenceFilters({ limit: 50 }, {})).toEqual({ limit: 50 });
  });
});
