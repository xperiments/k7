import { openDB, type DBSchema, type IDBPDatabase } from 'idb';
import { sha256Hex } from '../lib/sha256';

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
 *
 * Covers are content-addressed: `covers` holds one 422px AVIF per distinct source picture,
 * keyed by the SHA-256 of those source bytes, and a media row carries only `coverHash`. The
 * picture a row reads back is hydrated from that store, so the same album art is not copied
 * into every Track's row (it used to be: 278 MiB of source pictures for 25 MiB of distinct
 * images on the 3,485-track card).
 */
export interface CachedCover {
  bytes: Uint8Array<ArrayBuffer>;
  /** The picture's MIME type, e.g. `image/avif`. */
  format: string;
}

/** One stored cover, with the size the browser can draw it at without decoding. */
export interface CoverRecord extends CachedCover {
  width: number;
  height: number;
}

/**
 * The one seam between storage and the encoder: the page hands in `encodeCover`, the tests
 * hand in a stand-in. It turns one source picture into the cached artifact.
 */
export type CoverEncoder = (source: CachedCover) => Promise<CoverRecord>;

export interface CachedMedia {
  /** Path below the card's `music` directory. */
  path: string;
  title: string;
  artist: string;
  album: string;
  genre: string;
  /** The content-addressed cover this row points at. */
  coverHash?: string;
  /**
   * The hydrated picture. This is derived on read from `coverHash`; a row from before
   * content addressing may still carry the raw picture here, and a migration moves it.
   */
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
  /**
   * Which finalize this file has already taken: the `METADATA_WRITE_VERSION` of the tags and
   * label that were written into it. It is set only after a write finished, so a row without
   * it says "measured, but the file may still need its tags" — the old rows and a read-only
   * card. A re-scan that matches the file's size, mtime and hash *and* this number has
   * nothing to open the file for.
   */
  written?: number;
}

/**
 * Whether the card's file already carries this build's tags and label, as far as the record
 * can tell without opening it: the row is about the same bytes and a finalize for exactly
 * this writer completed. A row from another card cannot answer for this one — the caller
 * reads the row under this card's scope.
 */
export function writeCurrent(
  media: CachedMedia | undefined,
  hash: string,
  version: number,
): boolean {
  return media !== undefined && media.hash === hash && media.written === version;
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
  covers: {
    key: string;
    value: CoverRecord;
  };
}

/**
 * The store used to be `k7-media`, unscoped, one row per path. Neither of those can be
 * carried over: a row belongs to a card, and a row that never recorded one cannot be
 * attributed to any. The spec accepts the loss (§2), so nothing is migrated: the old database
 * is left where it is and the new one is keyed differently from the first byte.
 *
 * Version 3 adds the `covers` store. Version 2 rows keep their raw `cover` until
 * `migrateLegacyCovers()` has run; nothing is dropped on the upgrade itself, because the
 * cover cannot be re-encoded inside a synchronous IndexedDB version-change transaction.
 */
const DB_NAME = 'k7-media';
const DB_VERSION = 3;

let dbPromise: Promise<IDBPDatabase<MediaDB>> | null = null;

/** Open the store on first use, never at import: a module import must not need IndexedDB. */
function database(): Promise<IDBPDatabase<MediaDB>> {
  dbPromise ??= openDB<MediaDB>(DB_NAME, DB_VERSION, {
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
      if (!db.objectStoreNames.contains('covers')) db.createObjectStore('covers');
    },
  });
  return dbPromise;
}

/**
 * Read back the pictures a set of rows point at, with one `CachedCover` object per hash so
 * every view of an album shares one Blob URL rather than one per row.
 */
async function hydrate(rows: CachedMedia[]): Promise<CachedMedia[]> {
  const hashes = [...new Set(rows.map((row) => row.coverHash).filter((hash): hash is string => !!hash))];
  if (hashes.length === 0) return rows;

  const db = await database();
  const records = await Promise.all(hashes.map(async (hash) => [hash, await db.get('covers', hash)] as const));
  const pictures = new Map<string, CachedCover>();
  for (const [hash, record] of records) {
    if (record) pictures.set(hash, { bytes: record.bytes, format: record.format });
  }
  if (pictures.size === 0) return rows;

  return rows.map((row) => {
    if (!row.coverHash) return row;
    const cover = pictures.get(row.coverHash);
    return cover ? { ...row, cover } : row;
  });
}

/**
 * Store one track's tags and picture under a card's scope.
 *
 * The scope argument is authoritative: spreading it last means a `CachedMedia` that somehow
 * carries a `scope` cannot put the row on the wrong card. A row with a `coverHash` never
 * carries the picture bytes; the bytes live once, in `covers`.
 */
export async function putMedia(scope: string, media: CachedMedia): Promise<void> {
  let stored: StoredMedia;
  if (media.coverHash) {
    stored = { ...media, scope };
    delete stored.cover;
  } else {
    stored = { ...media, scope };
  }
  await (await database()).put('media', stored);
}

/** One track's cached media, for the card and path given. */
export async function getMedia(scope: string, path: string): Promise<CachedMedia | undefined> {
  const row = await (await database()).get('media', [scope, path]);
  if (!row) return undefined;
  return (await hydrate([row]))[0];
}

/**
 * Every cached row for one card, in path order. This is what the library source joins a
 * Snapshot's rows against; it never crosses a scope.
 */
export async function listMedia(scope: string): Promise<CachedMedia[]> {
  const rows = await (await database()).getAllFromIndex('media', 'scope', scope);
  return hydrate(rows);
}

/** How many tracks this card has cached, for the page's status line. */
export async function countMedia(scope: string): Promise<number> {
  return (await database()).countFromIndex('media', 'scope', scope);
}

/** Forget one card's media; every other card is untouched. Covers stay: they are shared. */
export async function clearMedia(scope: string): Promise<void> {
  const db = await database();
  // `[scope]` is a prefix of every `[scope, path]` key, and the empty array sorts above any
  // string in IndexedDB's key ordering, so this bound covers exactly one scope's rows.
  await db.delete('media', IDBKeyRange.bound([scope], [scope, []]));
}

/** One content-addressed cover, for the archive writer. */
export async function getCoverRecord(hash: string): Promise<CoverRecord | undefined> {
  return (await database()).get('covers', hash);
}

/** Every record named by `hashes`, in the order asked. Missing hashes are absent. */
export async function getCoverRecords(
  hashes: readonly string[],
): Promise<Map<string, CoverRecord>> {
  const db = await database();
  const out = new Map<string, CoverRecord>();
  await Promise.all(
    hashes.map(async (hash) => {
      const record = await db.get('covers', hash);
      if (record) out.set(hash, record);
    }),
  );
  return out;
}

/** Put one encoded cover under its content hash. */
export async function putCoverRecord(hash: string, record: CoverRecord): Promise<void> {
  await (await database()).put('covers', record, hash);
}

/** Forget every cached cover (tests and a manual cache reset). */
export async function clearCovers(): Promise<void> {
  await (await database()).clear('covers');
}

/** The encodings currently in flight, so two rows with one cover encode it once. */
const encoders = new Map<string, Promise<CoverRecord>>();

/**
 * The hash of a source picture, and the guarantee that its 422px AVIF exists in `covers`.
 *
 * The hash is of the **source** bytes, not the AVIF: a row's identity is the picture the file
 * carried, and the encoded bytes can change with the encoder without orphaning the row. If
 * the encoder fails, the source is stored as-is with its own MIME type, so a cover is never
 * lost to a missing codec.
 */
export async function ensureCover(source: CachedCover, encode: CoverEncoder): Promise<string> {
  const hash = await sha256Hex(source.bytes);
  if (await getCoverRecord(hash)) return hash;

  let pending = encoders.get(hash);
  if (!pending) {
    pending = (async () => {
      let record: CoverRecord;
      try {
        record = await encode(source);
      } catch {
        record = { bytes: source.bytes.slice() as Uint8Array<ArrayBuffer>, format: source.format, width: 0, height: 0 };
      }
      await putCoverRecord(hash, record);
      return record;
    })().finally(() => encoders.delete(hash));
    encoders.set(hash, pending);
  }
  await pending;
  return hash;
}

/**
 * Move every row from the v2 shape (raw `cover` bytes on the row) to a `coverHash` and one
 * shared `covers` record. This is what makes an existing cache shrink: a re-scan of a settled
 * card never opens its files, so without this pass the old rows would keep every copy.
 *
 * Idempotent and safe to call at every start; rows already carrying a hash are skipped.
 */
export async function migrateLegacyCovers(encode: CoverEncoder): Promise<number> {
  const db = await database();
  // Keys first, one row at a time: `getAll` would load every legacy cover (hundreds of
  // megabytes on a full card) into one array.
  const keys = await db.getAllKeys('media');
  let migrated = 0;

  for (const key of keys) {
    const row = await db.get('media', key);
    if (!row || !row.cover || row.coverHash) continue;
    const hash = await ensureCover({ bytes: row.cover.bytes, format: row.cover.format }, encode);
    const rest = { ...row };
    delete rest.cover;
    await db.put('media', { ...rest, coverHash: hash });
    migrated += 1;
  }
  return migrated;
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

/**
 * The four tags a Queue row shows, for a Track this browser has already measured.
 *
 * A re-scan that skips a file never opens it, so the row's genre has to come from somewhere
 * — and the media row is where it was kept the first time. The shape is `TrackTags` without
 * importing it: the store is storage, and the queue is what knows what a tag row is.
 */
export function tagsOf(media: CachedMedia): {
  title: string;
  artist: string;
  album: string;
  genre: string;
} {
  return {
    title: media.title,
    artist: media.artist,
    album: media.album,
    genre: media.genre,
  };
}
