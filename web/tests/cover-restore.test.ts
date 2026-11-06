import 'fake-indexeddb/auto';
import { beforeEach, describe, expect, it, vi } from 'vitest';
import { buildCoverArchive } from '../src/storage/coverArchive';
import { clearCovers, getCoverRecord } from '../src/storage/media';
import { restoreCoversFromCard, type CardDirectory } from '../src/ui/cardWriter';

const hash = 'a'.repeat(64);

function directory(bytes: Uint8Array<ArrayBuffer>, present = true): CardDirectory {
  return {
    values: async function* () {},
    getFileHandle: vi.fn(async (name: string) => {
      if (!present || name !== 'k7.covers') throw new Error('no such file');
      return { getFile: async () => new File([bytes], name) };
    }),
  } as unknown as CardDirectory;
}

beforeEach(async () => {
  await clearCovers();
});

describe('restoring covers from the card', () => {
  it('stores every record from k7.covers under its hash', async () => {
    const archive = buildCoverArchive([
      { hash, bytes: new Uint8Array([1, 2, 3]), format: 'image/avif', width: 422, height: 300 },
    ]);
    expect(await restoreCoversFromCard(directory(archive))).toBe(1);
    const record = await getCoverRecord(hash);
    expect(record?.format).toBe('image/avif');
    expect([...record!.bytes]).toEqual([1, 2, 3]);
  });

  it('leaves the cache alone when the card has no archive', async () => {
    expect(await restoreCoversFromCard(directory(new Uint8Array(), false))).toBe(0);
    expect(await getCoverRecord(hash)).toBeUndefined();
  });
});
