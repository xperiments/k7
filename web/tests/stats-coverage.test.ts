import { describe, expect, it } from 'vitest';
import { statsCoverage, type McpStats } from '../src/device/mcp';

/* The envelope the device actually sends (docs/specs/snapshot.md M3, and asserted by
 * host/tests/snapshot_tests.py): coverage lives *inside* snapshot. Reading it as a
 * top-level field took the whole Scan page down. */
const deviceAnswer: McpStats = {
  available: true,
  state: 'no_card',
  tracks: 5000,
  directories: 400,
  generation: 1,
  snapshot: {
    state: 'ok',
    format_version: 2,
    generation: 1767225600,
    rows: 4913,
    analyser: 300,
    model_set: '4660',
    coverage: { tracks: 5000, analysed: 4913, percent: 98 },
    tagged: { bpm: 812, key: 40, mood: 90, energy: 15 },
  },
};

describe('the coverage block', () => {
  it('comes out of the snapshot block, where the device puts it', () => {
    expect(statsCoverage(deviceAnswer)).toEqual({ tracks: 5000, analysed: 4913, percent: 98 });
  });

  it('is null when the device has no library at all', () => {
    expect(statsCoverage({ available: false, state: 'x', tracks: 0, directories: 0, generation: 0 }))
      .toBeNull();
  });

  it('is null for a Snapshot without a coverage block', () => {
    expect(
      statsCoverage({ ...deviceAnswer, snapshot: { state: 'no_update' } }),
    ).toBeNull();
  });

  it('is null when the block is not numbers', () => {
    expect(
      statsCoverage({
        ...deviceAnswer,
        snapshot: { state: 'ok', coverage: { tracks: 1 } as never },
      }),
    ).toBeNull();
  });

  it('is null for no answer at all', () => {
    expect(statsCoverage(null)).toBeNull();
  });
});
