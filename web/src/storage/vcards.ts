import { openDB, type DBSchema, type IDBPDatabase } from 'idb';

/**
 * The virtual cards this browser is preparing: card-shaped folders under the working
 * directory, each with a name and a soft capacity.
 *
 * The shelf is what a person sees *before* they have granted the working directory again, so
 * the list is IndexedDB, not the directory itself: it survives a reload with no permission and
 * no gesture. Each record is keyed by the card's mark id — the same id its `k7.card` carries —
 * so the record and the folder's own mark name one card.
 */

export interface VirtualCard {
  /** The card's mark id (a UUID v4): its identity, and the `mark:<id>` scope. */
  id: string;
  /** The name the user gave the card. */
  label: string;
  /** The subfolder's name under the working directory. */
  folder: string;
  /** The soft capacity, in bytes. A planning cap, never an allocation. */
  sizeBytes: number;
  /** The scope the card's stores are keyed by: `mark:<id>`. */
  scope: string;
  /** ISO 8601, UTC. */
  created: string;
}

interface VcardsDB extends DBSchema {
  cards: {
    key: string;
    value: VirtualCard;
  };
}

const DB_NAME = 'k7-vcards';
const DB_VERSION = 1;

let dbPromise: Promise<IDBPDatabase<VcardsDB>> | null = null;

function database(): Promise<IDBPDatabase<VcardsDB>> {
  dbPromise ??= openDB<VcardsDB>(DB_NAME, DB_VERSION, {
    upgrade(db) {
      if (!db.objectStoreNames.contains('cards')) {
        db.createObjectStore('cards', { keyPath: 'id' });
      }
    },
  });
  return dbPromise;
}

/** Every virtual card, oldest first, so the shelf is stable across reloads. */
export async function listVirtualCards(): Promise<VirtualCard[]> {
  const all = await (await database()).getAll('cards');
  return all.sort((left, right) => left.created.localeCompare(right.created));
}

/** One virtual card by its mark id. */
export async function getVirtualCard(id: string): Promise<VirtualCard | undefined> {
  return (await database()).get('cards', id);
}

/** Remember one virtual card. The record's own id is the key. */
export async function putVirtualCard(card: VirtualCard): Promise<void> {
  await (await database()).put('cards', card);
}

/** Forget one virtual card's record. */
export async function removeVirtualCard(id: string): Promise<void> {
  await (await database()).delete('cards', id);
}
