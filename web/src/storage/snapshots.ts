import { openDB } from 'idb';

/**
 * The last `xtape.db` this page built, kept across reloads.
 *
 * The player restarts after it accepts a snapshot — the write runs with the PSRAM cache off,
 * and the reply is sent first — so the page's origin disappears for a second or two. Before
 * this, that reboot could cost the whole run: the queue lived in memory, and a reload during
 * the restart meant measuring the card again. The image is the thing worth keeping — a few
 * kilobytes — and everything needed to send it again is in it.
 */
export interface StoredSnapshot {
  bytes: Uint8Array<ArrayBuffer>;
  tracks: number;
  /** Tracks outside `music/`, measured but not part of the card's Library. */
  skipped: number;
  /** When the image was built. */
  at: string;
  /** When the player last accepted it, if it has. */
  sentAt?: string;
}

const dbPromise = openDB('xtape-snapshots', 1, {
  upgrade(db) {
    db.createObjectStore('snapshots');
  },
});

export async function saveSnapshot(snapshot: StoredSnapshot): Promise<void> {
  await (await dbPromise).put('snapshots', snapshot, 'last');
}

export async function loadSnapshot(): Promise<StoredSnapshot | undefined> {
  return (await dbPromise).get('snapshots', 'last');
}

export async function clearSnapshot(): Promise<void> {
  await (await dbPromise).delete('snapshots', 'last');
}
