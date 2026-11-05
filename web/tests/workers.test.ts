import { describe, expect, it } from 'vitest';
import { autoWorkerCount } from '../src/audio/workers';

/**
 * The auto half of the worker pool: half the logical cores, a memory cap, and a hard four.
 * The boot probe can lower the count; this is the ceiling it starts from.
 */

describe('choosing the analysis worker count', () => {
  it('uses half the logical cores, up to four', () => {
    expect(autoWorkerCount(1, 8)).toBe(1);
    expect(autoWorkerCount(2, 8)).toBe(1);
    expect(autoWorkerCount(4, 8)).toBe(2);
    expect(autoWorkerCount(6, 8)).toBe(3);
    expect(autoWorkerCount(8, 8)).toBe(4);
    expect(autoWorkerCount(16, 32)).toBe(4);
  });

  it('caps by the memory the browser reports', () => {
    expect(autoWorkerCount(8, null)).toBe(4);
    expect(autoWorkerCount(8, 8)).toBe(4);
    expect(autoWorkerCount(8, 4)).toBe(3);
    expect(autoWorkerCount(8, 2)).toBe(2);
    expect(autoWorkerCount(8, 1)).toBe(1);
  });
});
