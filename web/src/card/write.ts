/**
 * Writing one file back to a card the page has open.
 *
 * The card is read-write only when it was *picked* (`showDirectoryPicker({ mode: 'readwrite' })`),
 * and that is the one handle this page may write a Track with. The queue's paths are the walk's
 * own — the chosen folder's name, then `music/`, then the Track — while the handle is the folder
 * itself, so a write is the same walk `card/read.ts` does for playing, in the other direction.
 *
 * A card is FAT32, where two spellings of a name are one entry, so every segment is resolved
 * folded: the handle's own lookup first, then a scan of the directory when the platform's
 * lookup is case-sensitive and the card's case differs from the path's.
 */

export interface CardWritableFile {
  kind: 'file';
  name: string;
  getFile(): Promise<File>;
  createWritable(): Promise<{
    write(data: Uint8Array): Promise<void>;
    close(): Promise<void>;
  }>;
}

export interface CardWritableDirectory {
  kind: 'directory';
  name: string;
  getFileHandle(name: string): Promise<CardWritableFile>;
  getDirectoryHandle(name: string): Promise<CardWritableDirectory>;
  values(): AsyncIterable<CardWritableFile | CardWritableDirectory>;
}

/**
 * The root as something that can be written, or null when it cannot: a test's stand-in, a
 * browser without the write side, or a handle that is not a directory at all.
 */
export function writableCard(root: unknown): CardWritableDirectory | null {
  const handle = root as Partial<CardWritableDirectory> | null;
  if (handle === null || typeof handle !== 'object') return null;
  if (typeof handle.getFileHandle !== 'function') return null;
  if (typeof handle.getDirectoryHandle !== 'function') return null;
  if (typeof handle.values !== 'function') return null;
  return handle as CardWritableDirectory;
}

async function childrenOf(
  directory: CardWritableDirectory,
): Promise<Array<CardWritableFile | CardWritableDirectory>> {
  const entries: Array<CardWritableFile | CardWritableDirectory> = [];
  for await (const entry of directory.values()) entries.push(entry);
  return entries;
}

/** The directory `name` inside `directory`, folded, or null. */
async function childDirectory(
  directory: CardWritableDirectory,
  name: string,
): Promise<CardWritableDirectory | null> {
  try {
    return await directory.getDirectoryHandle(name);
  } catch {
    /* The platform's lookup is case-sensitive: fall back to the directory's own names. */
  }
  const wanted = name.toLowerCase();
  const found = (await childrenOf(directory)).find(
    (entry) =>
      entry.kind === 'directory' && entry.name.toLowerCase() === wanted,
  );
  return found?.kind === 'directory' ? found : null;
}

/** The file `name` inside `directory`, folded, or null. */
async function childFile(
  directory: CardWritableDirectory,
  name: string,
): Promise<CardWritableFile | null> {
  try {
    return await directory.getFileHandle(name);
  } catch {
    /* Same as above: the name may be the card's, not the path's. */
  }
  const wanted = name.toLowerCase();
  const found = (await childrenOf(directory)).find(
    (entry) => entry.kind === 'file' && entry.name.toLowerCase() === wanted,
  );
  return found?.kind === 'file' ? found : null;
}

/**
 * Write `bytes` over the card's file at `path` and return the file as the card now holds it.
 *
 * `path` is the queue's path, rooted at the chosen directory's own name
 * (`<root>/music/Artist/Album/01.mp3`). Anything else — a dropped folder, a file that is no
 * longer there — returns null, and the caller leaves the card alone.
 */
export async function writeCardFile(
  root: CardWritableDirectory,
  path: string,
  bytes: Uint8Array<ArrayBufferLike>,
): Promise<File | null> {
  const segments = path.split('/').filter((segment) => segment.length > 0);
  if (segments.length < 2) return null;
  // The first segment is the picked folder's own name, which the handle already is.
  if (segments[0].toLowerCase() !== root.name.toLowerCase()) return null;

  let directory = root;
  for (const segment of segments.slice(1, -1)) {
    const next = await childDirectory(directory, segment);
    if (next === null) return null;
    directory = next;
  }

  const handle = await childFile(directory, segments[segments.length - 1]);
  if (handle === null) return null;

  const writable = await handle.createWritable();
  await writable.write(bytes);
  await writable.close();
  return handle.getFile();
}
