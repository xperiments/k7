import 'fake-indexeddb/auto';

import { beforeEach, describe, expect, it } from 'vitest';

import { artPackKey } from '../src/jcard/pack';
import {
  dropCachedArtPack,
  putCachedArtPack,
  readCachedArtPack,
} from '../src/storage/artPacks';
import { buildPack, seedDesigns } from './fixtures/art-pack';

/**
 * The J-card cache (`art-packs.md` §4): one user pack, keyed by the pack's CRC and size,
 * holding the J-cards and the header facts the page can show before the player answers.
 */

beforeEach(async () => {
  await dropCachedArtPack();
});

function pack(packId: string, count = 1) {
  return buildPack({
    designs: seedDesigns(count),
    packId,
    name: `${packId} pack`,
    author: 'A tester',
  });
}

describe('the active pack cache', () => {
  it('stores one pack under its CRC and size', async () => {
    const first = pack('first-pack', 2);
    const record = await putCachedArtPack(first);

    expect(record.key).toBe(artPackKey(first.packCrc, first.size));
    const read = await readCachedArtPack();
    expect(read).not.toBeNull();
    expect(read).toMatchObject({
      key: artPackKey(first.packCrc, first.size),
      packCrc: first.packCrc,
      size: first.size,
      sheetCrc: first.sheetCrc,
      designCount: 2,
      packId: 'first-pack',
      name: 'first-pack pack',
      author: 'A tester',
    });
    expect(read?.jcards).toEqual(first.jcards);
  });

  it('replaces the previous pack rather than collecting them', async () => {
    const first = pack('first-pack');
    const second = pack('second-pack', 2);
    await putCachedArtPack(first);
    await putCachedArtPack(second);

    const read = await readCachedArtPack();
    expect(read?.key).toBe(artPackKey(second.packCrc, second.size));
    expect(read?.packId).toBe('second-pack');
  });

  it('is empty after a drop, as a reset leaves it', async () => {
    await putCachedArtPack(pack('first-pack'));
    await dropCachedArtPack();
    expect(await readCachedArtPack()).toBeNull();
  });

  it('reads an empty cache as no pack, not as an error', async () => {
    await dropCachedArtPack();
    expect(await readCachedArtPack()).toBeNull();
  });
});
