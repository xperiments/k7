import { openDB } from 'idb';

/**
 * What the card's files carry besides their audio: their tags, and their picture.
 *
 * This is the half of a library view the player cannot serve. When the card is in the
 * player, the device knows every path and every measurement, and nothing about how a track
 * looks or what it is called. The browser read those once, while the card was in the
 * computer, so a playlist composed later - from the Snapshot's measurements joined to these
 * rows by path - can show a cover and a name without the card coming back.
 *
 * Keyed by the path below `music/`, which is the same key the Snapshot's rows use, so the
 * join is a lookup and not a guess.
 */
export interface CachedCover {
  bytes: Uint8Array<ArrayBuffer>;
  /** The picture's MIME type, e.g. `image/jpeg`; the row does not carry one. */
  format: string;
}

export interface CachedMedia {
  /** Path below the card's `music` directory. */
  path: string;
  title: string;
  artist: string;
  album: string;
  genre: string;
  cover?: CachedCover;
  at: string;
}

const dbPromise = openDB('xtape-media', 1, {
  upgrade(db) {
    db.createObjectStore('media', { keyPath: 'path' });
  },
});

export async function putMedia(media: CachedMedia): Promise<void> {
  await (await dbPromise).put('media', media);
}

export async function getMedia(path: string): Promise<CachedMedia | undefined> {
  return (await dbPromise).get('media', path);
}

export async function allMedia(): Promise<Map<string, CachedMedia>> {
  const rows = (await (await dbPromise).getAll('media')) as CachedMedia[];
  return new Map(rows.map((row) => [row.path, row]));
}

export async function countMedia(): Promise<number> {
  return (await dbPromise).count('media');
}

export async function clearMedia(): Promise<void> {
  await (await dbPromise).clear('media');
}
