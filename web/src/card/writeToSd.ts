/**
 * Writing a virtual card onto a mounted SD: the delta, the streamed copy, and the
 * verification.
 *
 * This is the finish act (ADR 0083). The SD is reached through a read-write pick whose root
 * becomes the card root; the virtual card's own subfolder is the source. Music is streamed
 * file by file into `<sd>/music/<path>` (never buffered whole), then read back and checked
 * for size and content hash before it is counted as written. `k7.db`, `k7.card` and
 * `k7.covers` are small: their source bytes are read, written to the SD root, and verified the
 * same way. `k7.card` travels byte-for-byte so the SD is recognised as the prepared card —
 * same mark id, never regenerated here.
 *
 * The delta is against the card's **scope** (the last-write record in `storage/vwrites.ts`),
 * not against the SD: two SDs written from one card share the card's mark and are the same
 * card to this store. Only music is delta'd — its current hash comes from the media store, so
 * an unchanged file is skipped without being read — and only a file that was written *and*
 * verified is remembered; a failed file is left out so the next write retries it.
 *
 * Nothing here touches the DOM or IndexedDB: the directory handles are stand-ins, the media
 * rows and the prior record are arguments, and the caller owns the picker and the store.
 */

import { sha256Hex } from '../lib/sha256';
import { walkMusic, type MusicEntry } from './compose';
import { measuredHash, type CachedMedia } from '../storage/media';

/** The write's view of a file: read it back, and write through it. */
export interface WriteFile {
  kind: 'file';
  name: string;
  getFile(): Promise<File>;
  createWritable(): Promise<{ write(data: Uint8Array): Promise<void>; close(): Promise<void> }>;
}

/** The write's view of a directory: reach children and files, and enumerate its names. */
export interface WriteDirectory {
  kind: 'directory';
  name: string;
  getDirectoryHandle(name: string, options?: { create?: boolean }): Promise<WriteDirectory>;
  getFileHandle(name: string, options?: { create?: boolean }): Promise<WriteFile>;
  values(): AsyncIterable<WriteDirectory | WriteFile>;
}

/** One music file the delta decided to copy, with its known hash when the media store has one. */
export interface PlannedFile {
  /** Path below `music/`, `/`-separated, no leading slash. */
  path: string;
  size: number;
  /** The content hash the media store recorded, or null when none is current (copy it). */
  hash: string | null;
}

/** The delta's answer: what to copy and what is already on the SD. */
export interface MusicPlan {
  copy: PlannedFile[];
  /** Unchanged music files, which keep their prior record entry verbatim. */
  skip: Array<{ path: string; hash: string }>;
}

/** Copy progress, for the page: files finished over files to copy. */
export interface WriteProgress {
  done: number;
  total: number;
}

/** What one write did. `written` + `skipped` is the whole of the next record's music files. */
export interface WriteOutcome {
  /** Music files written and verified this pass, with the hash to remember. */
  written: Array<{ path: string; hash: string }>;
  /** Unchanged music files, carrying their prior record hash. */
  skipped: Array<{ path: string; hash: string }>;
  /** Every path that failed to write or verify — music and root files alike. */
  failed: string[];
}

export interface WriteOptions {
  /** The card's cached media rows, joined by path to attach a current content hash. */
  media: readonly CachedMedia[];
  /** The prior record's files, as a path→hash map. Empty on a first write. */
  prior: ReadonlyMap<string, string>;
  onProgress?: (progress: WriteProgress) => void;
}

/** The card-root files, in the order a write copies them. */
const ROOT_FILES = ['k7.db', 'k7.card', 'k7.covers'] as const;

/**
 * The delta over one walk: which music files to copy and which to leave as they are.
 *
 * A file is copied when it has no current hash (no media row, or the row's size/mtime no
 * longer match), when the record has no entry for it (new), or when the recorded hash differs
 * from the current one. Only a current hash equal to the recorded one is skipped.
 */
export function planMusic(
  entries: readonly MusicEntry[],
  media: readonly CachedMedia[],
  prior: ReadonlyMap<string, string>,
): MusicPlan {
  const byPath = new Map(media.map((row) => [row.path, row]));
  const copy: PlannedFile[] = [];
  const skip: Array<{ path: string; hash: string }> = [];

  for (const entry of entries) {
    const hash = measuredHash(byPath.get(entry.path), {
      size: entry.size,
      lastModified: entry.mtime,
    });
    if (hash !== null && prior.get(entry.path) === hash) skip.push({ path: entry.path, hash });
    else copy.push({ path: entry.path, size: entry.size, hash });
  }

  return { copy, skip };
}

/**
 * Find a child directory by name, folded for case (a card is FAT32, where `MUSIC` is `music`).
 * The platform's own lookup is tried first, then a scan of the directory's names; only when
 * neither finds it and `create` is set is one made.
 */
async function directoryChild(
  directory: WriteDirectory,
  name: string,
  create: boolean,
): Promise<WriteDirectory> {
  try {
    return await directory.getDirectoryHandle(name, { create });
  } catch {
    /* The platform's lookup is case-sensitive; the card's own names may not be. */
  }
  try {
    for await (const child of directory.values()) {
      if (child.kind === 'directory' && child.name.toLowerCase() === name.toLowerCase()) {
        return child;
      }
    }
  } catch {
    /* A directory the browser will not enumerate falls through to the create. */
  }
  if (create) return directory.getDirectoryHandle(name, { create: true });
  throw new DOMException(`no ${name}`, 'NotFoundError');
}

/** Split a `/`-separated path into its parent segments and its file name. */
function pathParts(path: string): { parents: string[]; name: string } {
  const segments = path.split('/').filter((segment) => segment.length > 0);
  return { parents: segments.slice(0, -1), name: segments[segments.length - 1] };
}

/** Stream one music file into `<target>/music/<path>`, then verify size and hash. */
async function copyMusicFile(
  sourceMusic: WriteDirectory,
  targetMusic: WriteDirectory,
  file: PlannedFile,
): Promise<{ path: string; hash: string }> {
  const { parents, name } = pathParts(file.path);

  let sourceDir = sourceMusic;
  for (const parent of parents) sourceDir = await directoryChild(sourceDir, parent, false);
  const sourceFile = await (await sourceDir.getFileHandle(name)).getFile();

  let targetDir = targetMusic;
  for (const parent of parents) targetDir = await directoryChild(targetDir, parent, true);
  const targetHandle = await targetDir.getFileHandle(name, { create: true });

  // Streamed: one `write` per chunk from the source's own reader, so a large track is never
  // held whole in memory. `close` is reached even when a write fails, like `compose.ts`.
  const writable = await targetHandle.createWritable();
  const reader = sourceFile.stream().getReader();
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      await writable.write(value);
    }
  } finally {
    await writable.close();
  }

  // Verify before it counts: the target must be the same length, and — when the source has a
  // known hash — the same bytes. A mismatch throws, and the caller leaves it out of the record.
  const targetFile = await targetHandle.getFile();
  if (targetFile.size !== sourceFile.size) throw new Error(`${file.path}: size mismatch after copy`);
  const hash = await sha256Hex(await targetFile.arrayBuffer());
  if (file.hash !== null && hash !== file.hash) throw new Error(`${file.path}: hash mismatch after copy`);
  return { path: file.path, hash };
}

/** Write one small card-root file to the SD root and verify it the same way. */
async function copyRootFile(
  source: WriteDirectory,
  target: WriteDirectory,
  name: string,
): Promise<void> {
  let sourceHandle: WriteFile;
  try {
    sourceHandle = await source.getFileHandle(name);
  } catch {
    return; // A root file the card never wrote (no snapshot yet) is nothing to copy.
  }
  const sourceFile = await sourceHandle.getFile();
  const bytes = new Uint8Array(await sourceFile.arrayBuffer());

  const targetHandle = await target.getFileHandle(name, { create: true });
  const writable = await targetHandle.createWritable();
  await writable.write(bytes);
  await writable.close();

  const targetFile = await targetHandle.getFile();
  if (targetFile.size !== sourceFile.size) throw new Error(`${name}: size mismatch after copy`);
  const expected = await sha256Hex(bytes);
  const actual = await sha256Hex(await targetFile.arrayBuffer());
  if (actual !== expected) throw new Error(`${name}: hash mismatch after copy`);
}

/**
 * Write the virtual card's contents into the SD root.
 *
 * Music is walked, delta'd against the prior record and the media store, and each file to
 * copy is streamed into `<sd>/music/<path>`, verified, and only then counted. The three
 * card-root files are written after the music, and verified the same way. The result names
 * what was written, what was skipped, and what failed; a failed file is never in the written
 * set, so the caller's next record omits it and the next write retries it.
 */
export async function writeCardToSd(
  source: WriteDirectory,
  target: WriteDirectory,
  options: WriteOptions,
): Promise<WriteOutcome> {
  const plan = planMusic(await walkMusic(source), options.media, options.prior);

  // Probe which root files exist up front, so the progress total is honest.
  const presentRoots: string[] = [];
  for (const name of ROOT_FILES) {
    try {
      await source.getFileHandle(name);
      presentRoots.push(name);
    } catch {
      /* Absent: nothing to copy, not a failure. */
    }
  }

  const total = plan.copy.length + presentRoots.length;
  let done = 0;
  const report = (): void => options.onProgress?.({ done, total });
  report();

  const written: Array<{ path: string; hash: string }> = [];
  const failed: string[] = [];

  if (plan.copy.length > 0) {
    const sourceMusic = await directoryChild(source, 'music', false);
    const targetMusic = await directoryChild(target, 'music', true);
    for (const file of plan.copy) {
      try {
        written.push(await copyMusicFile(sourceMusic, targetMusic, file));
      } catch {
        failed.push(file.path);
      }
      done += 1;
      report();
    }
  }

  for (const name of presentRoots) {
    try {
      await copyRootFile(source, target, name);
    } catch {
      failed.push(name);
    }
    done += 1;
    report();
  }

  return { written, skipped: plan.skip, failed };
}
