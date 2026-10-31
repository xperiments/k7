/**
 * Writing `xtape.db` straight onto the card.
 *
 * A dropped folder is read-only for good: drag-and-drop hands over a handle whose
 * permission cannot be raised to `readwrite` (ADR 0010). The only way to write is
 * `showDirectoryPicker({ mode: 'readwrite' })`, which needs a user gesture — so the
 * pick happens on the click, before the snapshot is built, rather than after it.
 *
 * The picker is a parameter, not a global: nothing here is testable otherwise, and the
 * browser that has no picker at all (Safari, Firefox) simply reports `unsupported` and
 * the page falls back to the download.
 */

export interface SnapshotDirectory {
  getFileHandle(name: string, options?: { create?: boolean }): Promise<{
    createWritable(): Promise<{ write(data: Uint8Array): Promise<void>; close(): Promise<void> }>;
  }>;
}

export type DirectoryPicker = (options?: {
  mode?: 'read' | 'readwrite';
  id?: string;
}) => Promise<SnapshotDirectory>;

export type PickResult =
  | { kind: 'picked'; directory: SnapshotDirectory }
  | { kind: 'cancelled' }
  | { kind: 'unsupported' };

/** The browser's picker, or undefined where there is none. The returned function asks
 * for read-write: a card the page cannot write back to is a card the snapshot has to be
 * dragged onto by hand. */
export function cardPicker(): DirectoryPicker | undefined {
  const picker = (globalThis as { showDirectoryPicker?: DirectoryPicker }).showDirectoryPicker;
  if (typeof picker !== 'function') return undefined;
  return (options) => picker({ mode: 'readwrite', id: 'xtape-card', ...options });
}

/** Ask for the card's root. Cancelling is the user's answer, not an error. */
export async function pickCardDirectory(picker: DirectoryPicker | undefined): Promise<PickResult> {
  if (!picker) return { kind: 'unsupported' };
  try {
    const directory = await picker();
    return { kind: 'picked', directory };
  } catch (error) {
    if (error instanceof DOMException && error.name === 'AbortError') return { kind: 'cancelled' };
    throw error;
  }
}

/** Write the image as `name` in the chosen directory. */
export async function writeSnapshotToCard(
  directory: SnapshotDirectory,
  bytes: Uint8Array<ArrayBuffer>,
  name = 'xtape.db',
): Promise<void> {
  const handle = await directory.getFileHandle(name, { create: true });
  const writable = await handle.createWritable();
  await writable.write(bytes);
  await writable.close();
}
