import 'fake-indexeddb/auto';
import { beforeEach, describe, expect, it } from 'vitest';
import {
  getVirtualCard,
  listVirtualCards,
  putVirtualCard,
  removeVirtualCard,
  type VirtualCard,
} from '../src/storage/vcards';

const ID = 'f0b1c2d3-4e5f-4a6b-8c7d-9e0f1a2b3c4d';

const card = (overrides: Partial<VirtualCard> = {}): VirtualCard => ({
  id: ID,
  label: 'Road trip',
  folder: 'road-trip-f0b1c2d3',
  sizeBytes: 32_000_000_000,
  scope: `mark:${ID}`,
  created: '2026-11-12T00:00:00.000Z',
  ...overrides,
});

beforeEach(async () => {
  for (const stored of await listVirtualCards()) await removeVirtualCard(stored.id);
});

describe('the virtual-card store', () => {
  it('remembers a card and reads it back by id', async () => {
    await putVirtualCard(card());
    expect(await getVirtualCard(ID)).toEqual(card());
    expect(await getVirtualCard('missing')).toBeUndefined();
  });

  it('lists cards oldest first, so the shelf is stable', async () => {
    await putVirtualCard(card({ id: 'aaaaaaaa-0000-4000-8000-000000000000', created: '2026-11-12T02:00:00Z' }));
    await putVirtualCard(card({ id: 'bbbbbbbb-0000-4000-8000-000000000000', created: '2026-11-12T01:00:00Z' }));
    const list = await listVirtualCards();
    expect(list.map((c) => c.id)).toEqual([
      'bbbbbbbb-0000-4000-8000-000000000000',
      'aaaaaaaa-0000-4000-8000-000000000000',
    ]);
  });

  it('forgets a card', async () => {
    await putVirtualCard(card());
    await removeVirtualCard(ID);
    expect(await listVirtualCards()).toEqual([]);
  });
});
