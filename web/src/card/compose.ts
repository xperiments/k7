/**
 * Composing a virtual card: copying dropped audio into the card's `music/`, and the polling
 * reconciler that picks up files added, moved or removed outside the App.
 *
 * A virtual card is a Card that is not on an SD yet — a `music/` directory under a granted
 * working directory. Compose is the act that puts tracks there: the user drops a folder of
 * music, and each file is streamed into `music/` (never buffered whole), preserving the
 * structure below the folder the user dragged. Measurement is per-file — the queue takes a
 * track as soon as its copy lands — rather than a full-card scan after the walk ends.
 *
 * The browser has no filesystem watcher, so the reconciler is polling: it re-walks the open
 * card on a timer and diffs against the last known state. Only the pure, decision-heavy parts
 * live here — the path rule, the walk and the diff — so they can be tested without a browser.
 * The polling loop and the queue it feeds live in the analysis provider.
 *
 * Nothing here writes into a Folder (the user's music collection); the only directory a copy
 * touches is a card's own `music/` (`docs/specs/card-mark.md` K2).
 */

/** A dropped file and where it sat inside the folder the user dragged. */
export interface DroppedFile {
  /** The bytes to copy, streamed chunk by chunk; a dropped `File` is one such source. */
  file: { stream(): ReadableStream<Uint8Array> };
  /** Location inside the dropped folder, including the file name, `/`-separated. */
  path: string;
}

/** One file that finished copying, ready for the queue to measure. */
export interface CopiedFile {
  /** The file as the card now holds it, so its size and mtime are the copy's, not the source's. */
  file: File;
  /** Its Snapshot path below `music/`, `/`-separated, no leading slash. */
  path: string;
}

export interface ComposeResult {
  copied: number;
  skipped: number;
}

/** The write side of a file handle the copy needs, shaped like File System Access. */
export interface ComposeFileHandle {
  createWritable(): Promise<{ write(data: Uint8Array): Promise<void>; close(): Promise<void> }>;
  /** The file as written, for the caller that measures it once the copy lands. */
  getFile(): Promise<File>;
}

/** The card root the copy writes through: it reaches `music/` and creates files below it. */
export interface ComposeDirectory {
  getDirectoryHandle(name: string, options?: { create?: boolean }): Promise<ComposeDirectory>;
  getFileHandle(name: string, options?: { create?: boolean }): Promise<ComposeFileHandle>;
}

/**
 * The destination path below `music/` for one dropped file.
 *
 * A folder keeps its own name: dragging `A/` and `B/` lands as `music/A/…` and `music/B/…`,
 * because the folders are the shape the user chose and two of them must not merge. The one
 * exception is a dropped `music/` folder — the card's own root — whose name would nest a
 * second `music/`; its contents are what was meant, so that segment is dropped.
 */
export function composePath(path: string): string {
  const segments = path.split('/').filter((segment) => segment.length > 0);
  if (segments.length > 1 && segments[0].toLowerCase() === 'music') {
    return segments.slice(1).join('/');
  }
  return segments.join('/');
}

/**
 * Stream every dropped file into the card's `music/`, preserving structure, and report each
 * one to `onCopied` the moment its copy has closed. Copies are sequential so each file enters
 * the queue as it lands, while the next copy is still running.
 *
 * A file whose copy fails is counted as skipped, never silently lost; the caller decides what
 * to say. Reaching `music/` at all is the one hard failure and is thrown.
 */
export async function copyIntoMusic(
  card: ComposeDirectory,
  dropped: readonly DroppedFile[],
  onCopied: (copied: CopiedFile) => void,
  /** A folder below `music/` a drop lands in; `''` is the music root. */
  into = '',
): Promise<ComposeResult> {
  const music = await card.getDirectoryHandle('music', { create: true });
  const prefix = into.split('/').filter((segment) => segment.length > 0);

  let copied = 0;
  let skipped = 0;
  for (const entry of dropped) {
    const relative = composePath(entry.path);
    const segments = [...prefix, ...relative.split('/').filter((segment) => segment.length > 0)];
    if (segments.length === 0) {
      skipped += 1;
      continue;
    }
    const name = segments[segments.length - 1];
    const parents = segments.slice(0, -1);

    let file: File;
    try {
      let directory = music;
      for (const segment of parents) {
        directory = await directory.getDirectoryHandle(segment, { create: true });
      }
      const handle = await directory.getFileHandle(name, { create: true });
      const writable = await handle.createWritable();
      // Streamed: one `write` per chunk from the source's own reader, so a large track is
      // never held whole in memory. `close` commits and is reached even when a write fails.
      const reader = entry.file.stream().getReader();
      try {
        for (;;) {
          const { done, value } = await reader.read();
          if (done) break;
          await writable.write(value);
        }
      } finally {
        await writable.close();
      }
      file = await handle.getFile();
    } catch {
      skipped += 1;
      continue;
    }

    copied += 1;
    onCopied({ file, path: segments.join('/') });
  }

  return { copied, skipped };
}

/** One file the reconciler saw on the card, below `music/`. */
export interface MusicEntry {
  /** Snapshot path below `music/`, `/`-separated, no leading slash. */
  path: string;
  size: number;
  mtime: number;
}

/** What changed between two walks, so the caller can reconcile added, removed and changed. */
export interface Diff {
  added: MusicEntry[];
  removed: MusicEntry[];
  changed: MusicEntry[];
}

/** A file the walk enumerates, shaped like a `FileSystemFileHandle`. */
export interface WalkFile {
  kind: 'file';
  name: string;
  getFile(): Promise<File>;
}

/** A directory the walk enumerates, shaped like a `FileSystemDirectoryHandle`. */
export interface WalkDirectory {
  kind: 'directory';
  name: string;
  getDirectoryHandle(name: string, options?: { create?: boolean }): Promise<WalkDirectory>;
  values(): AsyncIterable<WalkFile | WalkDirectory>;
}

const ignored = (name: string) => name.startsWith('.') || name === '__MACOSX';

/** The `music` directory inside a card, matched folded: a card is FAT32, where `MUSIC` is
 * `music` (`card/mark.ts` K1). The platform's own lookup is tried first, then a scan. */
async function musicChild(card: WalkDirectory): Promise<WalkDirectory | null> {
  try {
    return await card.getDirectoryHandle('music');
  } catch {
    /* The platform's lookup is case-sensitive: fall back to the directory's own names. */
  }
  try {
    for await (const child of card.values()) {
      if (child.kind === 'directory' && child.name.toLowerCase() === 'music') return child;
    }
  } catch {
    /* A card the browser will not enumerate is a card with nothing to reconcile. */
  }
  return null;
}

/**
 * Walk the card's `music/` directory, listing every file below it with its size and mtime.
 * Hidden files are left out, exactly as the scan leaves them out. Nothing is read: size and
 * mtime come from each file's handle, which is all a diff needs.
 */
export async function walkMusic(card: WalkDirectory): Promise<MusicEntry[]> {
  const music = await musicChild(card);
  if (music === null) return [];

  const entries: MusicEntry[] = [];
  const walk = async (directory: WalkDirectory, base: string): Promise<void> => {
    const children: Array<WalkFile | WalkDirectory> = [];
    try {
      for await (const child of directory.values()) children.push(child);
    } catch {
      return; // a directory the browser will not enumerate is skipped, not fatal.
    }
    children.sort((left, right) => left.name.localeCompare(right.name));

    for (const child of children) {
      if (ignored(child.name)) continue;
      const path = base === '' ? child.name : `${base}/${child.name}`;
      if (child.kind === 'file') {
        try {
          const file = await child.getFile();
          entries.push({ path, size: file.size, mtime: file.lastModified });
        } catch {
          /* An unreadable file is not a reason to fail the walk. */
        }
      } else {
        await walk(child, path);
      }
    }
  };

  await walk(music, '');
  return entries;
}

/**
 * Diff two walks by path, size and mtime. `previous` is the last known state; a path in both
 * with the same size and mtime is unchanged, and its recorded content hash is still good —
 * that hash lives in the media store and is re-derived only when a file is re-measured, not
 * recomputed on every poll. `inProgress` names Snapshot paths a copy is still writing, which
 * are not ready to measure and are excluded from `added` and `changed`.
 */
export function diffMusic(
  previous: readonly MusicEntry[],
  current: readonly MusicEntry[],
  inProgress: ReadonlySet<string> = new Set(),
): Diff {
  const previousByPath = new Map(previous.map((entry) => [entry.path, entry]));
  const currentPaths = new Set(current.map((entry) => entry.path));

  const added: MusicEntry[] = [];
  const changed: MusicEntry[] = [];
  for (const entry of current) {
    if (inProgress.has(entry.path)) continue;
    const before = previousByPath.get(entry.path);
    if (before === undefined) added.push(entry);
    else if (before.size !== entry.size || before.mtime !== entry.mtime) changed.push(entry);
  }

  const removed: MusicEntry[] = [];
  for (const entry of previous) {
    if (!currentPaths.has(entry.path)) removed.push(entry);
  }

  return { added, removed, changed };
}
