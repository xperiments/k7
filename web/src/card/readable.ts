/**
 * A card's `File` is a snapshot, not a handle.
 *
 * Chrome's File System Access API returns a `File` that records the bytes at the moment
 * `getFile()` was called. If the file changes after that — the App's own tag write, another
 * tab, a sync tool, the OS reclaiming the snapshot under memory pressure on a long run — reads
 * from it fail with `NotReadableError`: *"The requested file could not be read, typically due
 * to permission problems that have occurred after a reference to a file was acquired."*
 *
 * The answer is not to trust the snapshot: ask the card for the file again and read that. This
 * module is the one retry rule, so every reader in the App treats a dead snapshot the same way.
 */
export type ReopenFile = () => Promise<File | null>;

/** Whether an error is the browser saying "this snapshot can no longer be read". */
export function isNotReadable(error: unknown): boolean {
  if (error === null || error === undefined) return false;
  if ((error as { name?: unknown }).name === 'NotReadableError') return true;
  /* A worker posts its errors as a message, so the name can be lost on the way; the
   * browser's sentence is unmistakable. */
  const message = error instanceof Error ? error.message : String(error);
  return /requested file could not be read/i.test(message);
}

/**
 * Run `use` on a file; if its snapshot is dead, reopen it once and run `use` on the fresh one.
 * Any other error is the caller's. A reopen that cannot produce a file rethrows the original
 * snapshot error, which names the cause the user can act on.
 */
export async function withReadable<T>(
  file: File,
  reopen: ReopenFile,
  use: (file: File) => Promise<T>,
): Promise<T> {
  try {
    return await use(file);
  } catch (error) {
    if (!isNotReadable(error)) throw error;
    const fresh = await reopen().catch(() => null);
    if (fresh === null) throw error;
    return use(fresh);
  }
}

export interface OpenedFile {
  /** The file the bytes came from: the original, or the fresh one a reopen produced. */
  file: File;
  bytes: Uint8Array<ArrayBuffer>;
}

/** Read a whole file, reopening its snapshot once if the card says it is no longer readable. */
export function openReadable(file: File, reopen: ReopenFile): Promise<OpenedFile> {
  return withReadable(file, reopen, async (source) => ({
    file: source,
    bytes: new Uint8Array(await source.arrayBuffer()),
  }));
}
