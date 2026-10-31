import { describe, expect, it } from 'vitest';
import { analysisWindow, moodWindow } from '../src/audio/analyzer';

describe('where in a track the measurements are taken', () => {
  it('takes mood from thirty seconds, a quarter in', () => {
    expect(moodWindow(300)).toEqual({ start: 75, duration: 30 });
  });

  it('takes tempo and key from two minutes, at the same anchor', () => {
    expect(analysisWindow(300)).toEqual({ start: 75, duration: 120 });
  });

  it('starts at the beginning when the track is shorter than the window', () => {
    expect(moodWindow(12)).toEqual({ start: 0, duration: 12 });
    expect(analysisWindow(45)).toEqual({ start: 0, duration: 45 });
  });

  it('never runs past the end', () => {
    for (const duration of [0, 1, 29, 30, 31, 60, 119, 120, 121, 600]) {
      for (const window of [moodWindow(duration), analysisWindow(duration)]) {
        expect(window.start).toBeGreaterThanOrEqual(0);
        expect(window.start + window.duration).toBeLessThanOrEqual(duration + 1e-9);
      }
    }
  });
});
