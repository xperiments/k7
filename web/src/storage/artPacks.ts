import { openDB, type DBSchema, type IDBPDatabase } from 'idb';

import { artPackKey, type ArtPack } from '../jcard/pack';

/**
 * The active user pack's J-cards, cached in this browser (`art-packs.md` §4).
 *
 * One pack is active at a time, so the store holds one record: the pack's J-cards and the
 * header facts the Art packs page can show before the player answers. The record's key is
 * the pack's **CRC and size** — one immutable file, so a cache hit cannot confuse two packs
 * whose names happen to be the same. The cache is not a second copy of the pack on the
 * device; it is what lets the covers draw at boot, before the first `GET /api/art/pack`.
 *
 * The J-cards are stored as the parsed JSON document, not as rendered designs: the reader
 * (`jcard/pack.ts`) is still the one gate when they come back, so a cache written by an
 * older build is re-checked rather than trusted.
 */

export interface CachedArtPack {
  /** `crc:size`, the record's key. */
  key: string;
  packCrc: number;
  size: number;
  sheetCrc: number;
  designCount: number;
  packId: string;
  name: string;
  author: string;
  /** The `{ v: 2, designs }` document, as `readArtPack` parsed it. */
  jcards: unknown;
}

const DB_NAME = 'k7-art';
const DB_VERSION = 1;
const STORE = 'packs';

interface ArtDB extends DBSchema {
  packs: {
    key: string;
    value: CachedArtPack;
  };
}

/**
 * Opened on the first cache call, not at import: the Analysis store may load this module in
 * a test or a worker that never touches art, and `indexedDB` is not there to open.
 */
let dbPromise: Promise<IDBPDatabase<ArtDB>> | null = null;

function db(): Promise<IDBPDatabase<ArtDB>> {
  dbPromise ??= openDB<ArtDB>(DB_NAME, DB_VERSION, {
    upgrade(database) {
      if (!database.objectStoreNames.contains(STORE)) {
        database.createObjectStore(STORE, { keyPath: 'key' });
      }
    },
  });
  return dbPromise;
}

/** Cache the active pack, replacing whatever one was there: the store holds one user pack. */
export async function putCachedArtPack(pack: ArtPack): Promise<CachedArtPack> {
  const record: CachedArtPack = {
    key: artPackKey(pack.packCrc, pack.size),
    packCrc: pack.packCrc,
    size: pack.size,
    sheetCrc: pack.sheetCrc,
    designCount: pack.designCount,
    packId: pack.packId,
    name: pack.name,
    author: pack.author,
    jcards: pack.jcards,
  };
  const transaction = (await db()).transaction(STORE, 'readwrite');
  await transaction.store.clear();
  await transaction.store.put(record);
  await transaction.done;
  return record;
}

/** The cached pack, or null when this browser has never cached one. */
export async function readCachedArtPack(): Promise<CachedArtPack | null> {
  const records = await (await db()).getAll(STORE);
  return records[0] ?? null;
}

/** Forget the cached J-cards: the reset's own drop, and a pack that came back unreadable. */
export async function dropCachedArtPack(): Promise<void> {
  await (await db()).clear(STORE);
}
