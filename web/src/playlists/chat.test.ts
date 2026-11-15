import { describe, expect, it } from 'vitest';
import type { McpSearch, SearchArgs } from '../device/mcp';
import {
  answerText,
  filterWords,
  nameFor,
  READ_ONLY_SOURCE,
  addQuestion,
  addedNote,
  SAVED_NOTE,
} from './chat';

function search(partial: Partial<McpSearch> = {}): McpSearch {
  return {
    query: '',
    matched: 0,
    tracks: [],
    truncated: false,
    ...partial,
  };
}

const INTENT: SearchArgs = {
  genre: 'techno',
  bpmMin: 124,
  bpmMax: 132,
  key: ['8A'],
  mood: ['party'],
  limit: 20,
};

describe('filterWords', () => {
  it('says the filters in the words a person used, and leaves paging out', () => {
    expect(filterWords(INTENT)).toEqual([
      'techno',
      'party',
      'key 8A',
      '124–132 BPM',
    ]);
  });

  it('says one-sided bounds as one-sided', () => {
    expect(filterWords({ bpmMin: 125 })).toEqual(['from 125 BPM']);
    expect(filterWords({ bpmMax: 95 })).toEqual(['up to 95 BPM']);
    expect(filterWords({ energyMin: 70, energyMax: 100 })).toEqual(['energy 70–100']);
  });

  it('says a track length as a clock, one-sided or two', () => {
    expect(filterWords({ durationMax: 180 })).toEqual(['length up to 3:00']);
    expect(filterWords({ durationMin: 60 })).toEqual(['length from 1:00']);
    expect(filterWords({ durationMin: 60, durationMax: 240 })).toEqual(['length 1:00–4:00']);
  });

  it('quotes a name and names the sort', () => {
    expect(filterWords({ query: 'Daft Punk', sort: 'bpm' })).toEqual([
      '“Daft Punk”',
      'sorted by BPM',
    ]);
  });

  it('has nothing to say about an empty Intent', () => {
    expect(filterWords({})).toEqual([]);
    expect(filterWords({ limit: 10, offset: 20 })).toEqual([]);
  });
});

describe('answerText', () => {
  it('counts what was found and says what was asked', () => {
    const text = answerText({
      intent: INTENT,
      search: search({
        matched: 18,
        tracks: [{ path: 'a' }, { path: 'b' }] as McpSearch['tracks'],
      }),
    });
    expect(text).toBe('Found 2 tracks — techno, party, key 8A, 124–132 BPM.');
  });

  it('says the whole count when the page is cut', () => {
    const tracks = Array.from({ length: 20 }, (_, index) => ({ path: `t${index}` }));
    const text = answerText({
      intent: { genre: 'techno' },
      search: search({ matched: 240, tracks: tracks as McpSearch['tracks'], truncated: true }),
    });
    expect(text).toBe('Found 20 of 240 tracks — techno.');
  });

  it('says nothing matched with the filters that ran', () => {
    expect(answerText({ intent: INTENT, search: search() })).toBe(
      'Nothing matched techno, party, key 8A, 124–132 BPM.',
    );
    expect(answerText({ intent: {}, search: search() })).toBe('Nothing matched.');
  });

  it('says why a relaxed answer lost its ranges', () => {
    const text = answerText({
      intent: { mood: ['relaxed'] },
      search: search({ matched: 1, tracks: [{ path: 'a' }] as McpSearch['tracks'] }),
      relaxed: true,
    });
    expect(text).toBe(
      'Those ranges found nothing, so I dropped them. Found 1 track — relaxed.',
    );
  });

  it('counts one track as one track', () => {
    const text = answerText({
      intent: { query: 'Rainy Sunday' },
      search: search({ matched: 1, tracks: [{ path: 'a' }] as McpSearch['tracks'] }),
    });
    expect(text).toBe('Found 1 track — “Rainy Sunday”.');
  });
});

describe('the save question', () => {
  it('asks about tracks, or about one', () => {
    expect(addQuestion(18, 'Late night 8A')).toBe(
      'Add these 18 tracks to “Late night 8A”?',
    );
    expect(addQuestion(1, 'Late night 8A')).toBe('Add this track to “Late night 8A”?');
    expect(addedNote(18, 'Late night 8A')).toBe('Added 18 tracks to “Late night 8A”.');
    expect(addedNote(1, 'Late night 8A')).toBe('Added 1 track to “Late night 8A”.');
  });

  it('names the limit of a read-only source, and the other choice', () => {
    expect(READ_ONLY_SOURCE).toContain('another card');
    expect(SAVED_NOTE).toContain('new list');
  });
});

describe('nameFor', () => {
  it('keeps one line, and says what a blank ask becomes', () => {
    expect(nameFor('  10 songs\n for a workout  ')).toBe('10 songs for a workout');
    expect(nameFor('   ')).toBe('Asked set');
    expect(nameFor('x'.repeat(80))).toHaveLength(60);
  });
});
