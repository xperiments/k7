import { openDB, type DBSchema } from 'idb';

/**
 * The last Update file this page built, kept across reloads.
 *
 * The player restarts after it accepts a snapshot — the write runs with the PSRAM cache off,
 * and the reply is sent first — so the page's origin disappears for a second or two. Before
 * this, that reboot could cost the whole run: the queue lived in memory, and a reload during
 * the restart meant measuring the card again. The image is the thing worth keeping — a few
 * kilobytes — and everything needed to send it again is in it.
 *
 * An image belongs to a **card**, not to the browser (`docs/specs/card-mark.md` §2, M1): one
 * Last image for every card is how a send goes to the wrong one. The store is keyed by
 * **Scope** — `mark:<id>` or `fingerprint:<sha256>` — instead of the literal key `last`.
 */

/** What the image was built for, when the App knew more than the scope's prefix. */
export interface SnapshotCard {
  /**
   * The card's mark id, when the image was built for a marked card. The raw id, not the
   * `mark:` prefix: `scope` already carries the prefix, and this records provenance.
   */
  mark?: string;
  /**
   * The card's content fingerprint as lowercase hex, when no usable mark existed. Again
   * without the `fingerprint:` prefix.
   */
  fingerprint?: string;
}

export interface StoredSnapshot {
  /** The card this image belongs to: `mark:<id>` or `fingerprint:<sha256>`. */
  scope: string;
  /** The identity the scope was derived from, for saying which card the image is for. */
  card: SnapshotCard;
  bytes: Uint8Array<ArrayBuffer>;
  tracks: number;
  /** Tracks outside `music/`, measured but not part of the card's Library. */
  skipped: number;
  /** When the image was built. */
  at: string;
  /** When the player last accepted it, if it has. */
  sentAt?: string;
}

interface SnapshotDB extends DBSchema {
  snapshots: {
    key: string;
    value: StoredSnapshot;
  };
}

/**
 * The store used to be `xtape-snapshots`, with one record at the out-of-line key `last`.
 * That record cannot be assigned to a card after the fact, so it is discarded rather than
 * migrated; the card-mark spec accepts the loss (§2). Version 2 also recreates the store if
 * a first scoped attempt at version 1 is found, whose layout may differ.
 */
const DB_NAME = 'k7-snapshots';
const DB_VERSION = 2;


const dbPromise = openDB<SnapshotDB>(DB_NAME, DB_VERSION, {
  upgrade(db, oldVersion) {
    if (oldVersion < 2 && db.objectStoreNames.contains('snapshots')) {
      // The old store used out-of-line keys ('last'). The new one keys on the record's own
      // scope, which the old records do not have, so there is nothing to copy forward.
      db.deleteObjectStore('snapshots');
    }

    if (!db.objectStoreNames.contains('snapshots')) {
      db.createObjectStore('snapshots', { keyPath: 'scope' });
    }
  },
});

/**
 * Keep one image for one card. The scope argument wins over the record's own field, so an
 * image can never be filed under a scope other than the one being saved for.
 */
export async function saveSnapshot(scope: string, snapshot: StoredSnapshot): Promise<void> {
  await (await dbPromise).put('snapshots', { ...snapshot, scope });
}

/** The Last image built for this card, if this browser has one. */
export async function loadSnapshot(scope: string): Promise<StoredSnapshot | undefined> {
  return (await dbPromise).get('snapshots', scope);
}

/** Forget this card's Last image; every other card keeps its own. */
export async function clearSnapshot(scope: string): Promise<void> {
  await (await dbPromise).delete('snapshots', scope);
}
