/**
 * Reading one file back off a card the page has open.
 *
 * The library names a track by its path below the card's `music` directory — the same string
 * the Snapshot is keyed by — while the handle the picker returns is the card root. So playing
 * a track the shelf names means walking `music/` (in whatever case the card was formatted
 * with) and then each segment, and reading the file the card holds now. Nothing is cached:
 * this is what the card has, which is the point when the card is in the computer.
 *
 * The handle is structural: a real `FileSystemDirectoryHandle` fits, and so does a test's
 * stand-in.
 */

export interface ReadableFile {
  kind: 'file';
  name: string;
  getFile(): Promise<File>;
}

export interface ReadableDirectory {
  kind: 'directory';
  name: string;
  values(): AsyncIterable<ReadableEntry>;
}

export type ReadableEntry = ReadableFile | ReadableDirectory;

/** A card root, or anything a test says is one. */
export type CardRoot = ReadableDirectory;

async function children(directory: ReadableDirectory): Promise<ReadableEntry[]> {
  const found: ReadableEntry[] = [];
  for await (const child of directory.values()) found.push(child);
  return found;
}

/** One child by name, folded: a card is FAT32, where `Music` and `music` are one directory. */
async function child(
  directory: ReadableDirectory,
  name: string,
): Promise<ReadableEntry | null> {
  const wanted = name.toLowerCase();
  const found = (await children(directory)).find((entry) => entry.name.toLowerCase() === wanted);
  return found ?? null;
}

/**
 * The file the card holds at `path` — `Artist/Album/01 - Track.mp3`, below `music` — or
 * `null` when the card no longer has it. A missing directory is a missing file, never an
 * error: the shelf can name a track a card no longer holds.
 */
export async function readCardFile(root: CardRoot, path: string): Promise<File | null> {
  const segments = path.split('/').filter((segment) => segment.length > 0);
  const name = segments.pop();
  if (name === undefined) return null;

  const music = await child(root, 'music');
  if (music === null || music.kind !== 'directory') return null;

  let directory: ReadableDirectory = music;
  for (const segment of segments) {
    const next = await child(directory, segment);
    if (next === null || next.kind !== 'directory') return null;
    directory = next;
  }

  const file = await child(directory, name);
  if (file === null || file.kind !== 'file') return null;
  return file.getFile();
}
