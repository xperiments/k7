import { describe, expect, it } from 'vitest';

import { OP_SPECS, isOpName, type OpName } from '../src/device/protocol';

/**
 * The device's operation table is the one list of names the transports may ask. It is small
 * on purpose: the names are the contract `direct.ts` (and through it `local.ts`) makes
 * against `/api/…` and `/mcp`, so a name missing here is an operation nothing can call and a
 * stray name here is a call the device does not answer.
 */

const OPS: readonly OpName[] = [
  'status',
  'playlists',
  'playlist',
  'select',
  'rescan',
  'play',
  'library.search',
  'library.list',
  'library.stats',
  'library.track',
  'stream.play',
  'stream.pause',
  'stream.volume',
  'stream.state',
];

describe('the device operation table', () => {
  it('names every operation, and nothing else', () => {
    expect(Object.keys(OP_SPECS).sort()).toEqual([...OPS].sort());
  });

  it('gives each operation a finite, positive deadline', () => {
    for (const op of OPS) {
      const { timeout } = OP_SPECS[op];
      expect(Number.isFinite(timeout), `${op} timeout`).toBe(true);
      expect(timeout, `${op} timeout`).toBeGreaterThan(0);
    }
  });

  it('recognises exactly those names, and no other value', () => {
    for (const op of OPS) expect(isOpName(op), op).toBe(true);
    for (const value of [
      '',
      'nope',
      'library',
      'library.',
      'status ',
      'Status',
      42,
      null,
      undefined,
      {},
      ['status'],
    ]) {
      expect(isOpName(value), String(value)).toBe(false);
    }
  });
});
