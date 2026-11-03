import { openDB, type DBSchema } from 'idb';

/**
 * What the card's files carry besides their audio: their tags, and their picture.
 *
 * This is the half of a library view the player cannot serve. When the card is in the
 * player, the device knows every path and every measurement, and nothing about how a track
 * looks or what it is called. The browser read those once, while the card was in the
 * computer, so a playlist composed later - from the Snapshot's measurements joined to these
 * rows by path - can show a cover and a name without the card coming back.
 *
 * The join's other half is the **Scope** (`docs/specs/card-mark.md` §2, M1). A path alone is
 * not an identity: `Artist/Album/01.mp3` on card B would otherwise inherit card A's cover
 * and title. The store is keyed by `[scope, path]`, where `scope` is `mark:<id>` or
 * `fingerprint:<sha256>`. The same path on two cards is two rows, and a clone - which shares
 * a mark - shares them deliberately.
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
  /**
   * The file as it was measured: its size, its modification time and the SHA-256 of its
   * bytes. A re-scan compares these against the card's own files, so a Track this browser
   * has already measured is recognised without being read again. Rows from before these
   * fields existed have none, and are measured once more rather than guessed at.
   */
  size?: number;
  mtime?: number;
  hash?: string;
}

/** The row as stored: the media plus the card it was read from. */
interface StoredMedia extends CachedMedia {
  scope: string;
}

interface MediaDB extends DBSchema {
  media: {
    key: [string, string];
    value: StoredMedia;
    indexes: { scope: string };
  };
}

/**
 * The store used to be `k7-media`, unscoped, one row per path. Neither of those can be
 * carried over: a row belongs to a card, and a row that never recorded one cannot be
 * attributed to any. The spec accepts the loss (§2), so nothing is migrated: the old database
 * is left where it is and the new one is keyed differently from the first byte.
 *
 * The version is 2 because the first scoped attempt on this name would have been 1; an
 * upgrade from it recreates the store too, rather than failing on a key-path mismatch.
 */
const DB_NAME = 'k7-media';
const DB_VERSION = 2;


const dbPromise = openDB<MediaDB>(DB_NAME, DB_VERSION, {
  upgrade(db, oldVersion) {
    if (oldVersion < 2) {
      // v0/v1 held `media` with keyPath 'path' (or not at all). Recreate it: there is no
      // scope to backfill, and doing so would be a guess about which card each row came
      // from.
      if (db.objectStoreNames.contains('media')) db.deleteObjectStore('media');
    }

    if (!db.objectStoreNames.contains('media')) {
      const store = db.createObjectStore('media', { keyPath: ['scope', 'path'] });
      // Listing one card's rows and counting them are both scope queries; the compound
      // primary key alone would force a scan of every card.
      store.createIndex('scope', 'scope');
    }
  },
});

/**
 * Store one track's tags and picture under a card's scope.
 *
 * The scope argument is authoritative: spreading it last means a `CachedMedia` that somehow
 * carries a `scope` cannot put the row on the wrong card.
 */
export async function putMedia(scope: string, media: CachedMedia): Promise<void> {
  await (await dbPromise).put('media', { ...media, scope });
}

/** One track's cached media, for the card and path given. */
export async function getMedia(scope: string, path: string): Promise<CachedMedia | undefined> {
  return (await dbPromise).get('media', [scope, path]);
}

/**
 * Every cached row for one card, in path order. This is what the library source joins a
 * Snapshot's rows against; it never crosses a scope.
 */
export async function listMedia(scope: string): Promise<CachedMedia[]> {
  return (await dbPromise).getAllFromIndex('media', 'scope', scope);
}

/** How many tracks this card has cached, for the page's status line. */
export async function countMedia(scope: string): Promise<number> {
  return (await dbPromise).countFromIndex('media', 'scope', scope);
}

/** Forget one card's media; every other card is untouched. */
export async function clearMedia(scope: string): Promise<void> {
  const db = await dbPromise;
  // `[scope]` is a prefix of every `[scope, path]` key, and the empty array sorts above any
  // string in IndexedDB's key ordering, so this bound covers exactly one scope's rows.
  await db.delete('media', IDBKeyRange.bound([scope], [scope, []]));
}

/**
 * The content hash of the file a row describes, or null when the row is not about that file.
 *
 * A re-scan has the card's files in hand and no reason to read them: path, size and
 * modification time are enough to know whether this browser already measured the bytes, and
 * the hash the row was stored with is what names the analysis. A row without those fields is
 * from before they existed — measured again, never guessed at.
 */
export function measuredHash(
  media: CachedMedia | undefined,
  file: { size: number; lastModified: number },
): string | null {
  if (!media || media.hash === undefined) return null;
  if (media.size !== file.size || media.mtime !== file.lastModified) return null;
  return media.hash;
}
