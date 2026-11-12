import { openDB, type DBSchema, type IDBPDatabase } from 'idb';

/**
 * The last "write to SD" a virtual card took: the music files that write copied and verified,
 * each by its content hash, so the next write re-copies only what changed.
 *
 * The record is keyed by the card's **scope**, not by the physical SD it landed on. The SD
 * carries the card's own mark, so two SDs written from one card are indistinguishable, and a
 * write to either one says the same thing about the card's bytes — the next write is a delta
 * against the card, not against a particular stick (ADR 0083).
 *
 * Only music files are remembered. `k7.db`, `k7.card` and `k7.covers` are small and carry
 * state that must reach the SD byte-for-byte, so they are written on every pass; there is no
 * media row to key their hash on the way the music has.
 */

/** One music file a write verified, named by its path below the card's `music/`. */
export interface WrittenFile {
  /** Path below `music/`, `/`-separated, no leading slash. */
  path: string;
  /** SHA-256 of the bytes as written and verified, lowercase hex. */
  hash: string;
}

/** What one card's last write recorded. */
export interface VirtualWrite {
  /** The card's scope (`mark:<id>`): the key the record is stored under. */
  scope: string;
  /** The music files the last write wrote and verified, in path order. */
  files: WrittenFile[];
  /** ISO 8601, UTC, when the write finished. */
  at: string;
}

interface VWritesDB extends DBSchema {
  writes: {
    key: string;
    value: VirtualWrite;
  };
}

const DB_NAME = 'k7-vwrites';
const DB_VERSION = 1;

let dbPromise: Promise<IDBPDatabase<VWritesDB>> | null = null;

function database(): Promise<IDBPDatabase<VWritesDB>> {
  dbPromise ??= openDB<VWritesDB>(DB_NAME, DB_VERSION, {
    upgrade(db) {
      if (!db.objectStoreNames.contains('writes')) {
        db.createObjectStore('writes', { keyPath: 'scope' });
      }
    },
  });
  return dbPromise;
}

/** The last write this card took, or undefined when it has never been written. */
export async function getWrite(scope: string): Promise<VirtualWrite | undefined> {
  return (await database()).get('writes', scope);
}

/** Remember one card's last write. The record's own `scope` is the key. */
export async function putWrite(write: VirtualWrite): Promise<void> {
  await (await database()).put('writes', write);
}

/** Forget one card's write record — a card being deleted, and a test's reset. */
export async function removeWrite(scope: string): Promise<void> {
  await (await database()).delete('writes', scope);
}
