import { describe, expect, it } from 'vitest';
import { parseEnergyTag } from '../src/domain/energyTag';

describe('energy tags', () => {
  it('accepts 0–1 values', () => {
    expect(parseEnergyTag(0.8)).toBe(0.8);
    expect(parseEnergyTag('0.8')).toBe(0.8);
    expect(parseEnergyTag('1')).toBe(1);
    expect(parseEnergyTag('0')).toBe(0);
  });

  it('accepts percentages and 0–100 scales', () => {
    expect(parseEnergyTag('80%')).toBe(0.8);
    expect(parseEnergyTag('80')).toBe(0.8);
    expect(parseEnergyTag(40)).toBe(0.4);
    expect(parseEnergyTag('100')).toBe(1);
  });

  it('rejects values it cannot place', () => {
    expect(parseEnergyTag('high')).toBeNull();
    expect(parseEnergyTag('')).toBeNull();
    expect(parseEnergyTag(-1)).toBeNull();
    expect(parseEnergyTag(101)).toBeNull();
    expect(parseEnergyTag(1.5)).toBeNull();
    expect(parseEnergyTag(undefined)).toBeNull();
  });
});
