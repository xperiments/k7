import { describe, expect, it } from 'vitest';
import type { SearchArgs } from '../device/mcp';
import {
  filterWords,
  nameFor,
  READ_ONLY_SOURCE,
  addedNote,
  SAVED_NOTE,
} from './chat';

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


describe('the added note', () => {
  it('says what arrived, and how much of it', () => {
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
