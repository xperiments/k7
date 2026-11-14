/** The retry after an exact search finds nothing. */

import { describe, expect, it } from 'vitest';

import { relaxRanges } from './relax';

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
