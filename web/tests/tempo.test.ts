import { describe, expect, it } from 'vitest';
import {
  bpmFromBeats,
  foldTempo,
  median,
  normaliseDeepConfidence,
  shouldEscalate,
  tempoConfidence,
} from '../src/domain/tempo';

const steady = (bpm: number, count = 33) =>
  Array.from({ length: count }, (_, i) => i * (60 / bpm));

describe('foldTempo', () => {
  it('leaves an in-range pulse alone', () => {
    expect(foldTempo(128)).toBe(128);
    expect(foldTempo(70)).toBe(70);
    expect(foldTempo(180)).toBe(180);
  });

  it('doubles slow pulses and halves fast ones', () => {
    expect(foldTempo(64)).toBe(128);
    expect(foldTempo(40)).toBe(80);
    expect(foldTempo(256)).toBe(128);
    expect(foldTempo(348)).toBe(174);
    expect(foldTempo(400)).toBe(100);
  });

  it('rejects degenerate input', () => {
    expect(foldTempo(0)).toBe(0);
    expect(foldTempo(Number.NaN)).toBe(0);
    expect(foldTempo(-120)).toBe(0);
  });
});

describe('median and bpm', () => {
  it('computes the median of odd and even lists', () => {
    expect(median([3, 1, 2])).toBe(2);
    expect(median([4, 1, 3, 2])).toBe(2.5);
    expect(median([])).toBe(0);
  });

  it('derives BPM from beat times', () => {
    expect(bpmFromBeats(steady(120))).toBeCloseTo(120, 6);
    expect(bpmFromBeats([0])).toBe(0);
  });
});

describe('tempo confidence', () => {
  it('is high for a steady pulse', () => {
    const beats = steady(120);
    expect(tempoConfidence(beats)).toBeCloseTo(1, 6);
    expect(shouldEscalate(tempoConfidence(beats), beats.length)).toBe(false);
  });

  it('is zero when there are too few beats', () => {
    expect(tempoConfidence([0, 0.5])).toBe(0);
    expect(shouldEscalate(0, 2)).toBe(true);
  });

  it('drops when the pulse changes', () => {
    const beats = [...steady(120, 16), ...steady(120, 17).map((t) => t + 16 * 0.5 + 0.4)];
    const confidence = tempoConfidence(beats);
    expect(confidence).toBeLessThan(1);
  });

  it('escalates below the threshold', () => {
    expect(shouldEscalate(0.59, 100)).toBe(true);
    expect(shouldEscalate(0.6, 100)).toBe(false);
    expect(shouldEscalate(0.1, 4)).toBe(true);
  });
});

describe('deep confidence', () => {
  it('maps 0–5.32 onto 0–1', () => {
    expect(normaliseDeepConfidence(0)).toBe(0);
    expect(normaliseDeepConfidence(5.32)).toBe(1);
    expect(normaliseDeepConfidence(1.5)).toBeCloseTo(0.282, 3);
    expect(normaliseDeepConfidence(9)).toBe(1);
  });
});
