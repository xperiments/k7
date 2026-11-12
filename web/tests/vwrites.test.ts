import 'fake-indexeddb/auto';
import { beforeEach, describe, expect, it } from 'vitest';
import {
  getWrite,
  putWrite,
  removeWrite,
  type VirtualWrite,
} from '../src/storage/vwrites';

const SCOPE = 'mark:f0b1c2d3-4e5f-4a6b-8c7d-9e0f1a2b3c4d';

const write = (overrides: Partial<VirtualWrite> = {}): VirtualWrite => ({
  scope: SCOPE,
  files: [
    { path: 'Artist/Album/01.mp3', hash: 'a'.repeat(64) },
    { path: 'Artist/Album/02.mp3', hash: 'b'.repeat(64) },
  ],
  at: '2026-11-12T00:00:00.000Z',
  ...overrides,
});

beforeEach(async () => {
  await removeWrite(SCOPE);
});

describe('the last-write store', () => {
  it('remembers a write and reads it back by scope', async () => {
    await putWrite(write());
    expect(await getWrite(SCOPE)).toEqual(write());
    expect(await getWrite('mark:other')).toBeUndefined();
  });

  it('replaces a card’s write rather than stacking records', async () => {
    await putWrite(write());
    await putWrite(write({ files: [{ path: 'only.mp3', hash: 'c'.repeat(64) }] }));
    const stored = await getWrite(SCOPE);
    expect(stored?.files).toEqual([{ path: 'only.mp3', hash: 'c'.repeat(64) }]);
  });

  it('forgets a write', async () => {
    await putWrite(write());
    await removeWrite(SCOPE);
    expect(await getWrite(SCOPE)).toBeUndefined();
  });
});
