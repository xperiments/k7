/**
 * Writing `k7.db` straight onto the card.
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
  /** The picked folder's own name. The handle a real picker returns always has one; a
   * test's stand-in may not, and then nothing is written back to it. */
  name?: string;
  getFileHandle(name: string, options?: { create?: boolean }): Promise<{
    createWritable(): Promise<{ write(data: Uint8Array): Promise<void>; close(): Promise<void> }>;
  }>;
}

/** What the mark reader needs beyond writing: the names in a directory, and a file's text. */
export interface CardEntryHandle {
  kind: 'file' | 'directory';
  name: string;
}

export interface CardFileHandle {
  getFile(): Promise<File>;
  createWritable(): Promise<{ write(data: Uint8Array): Promise<void>; close(): Promise<void> }>;
}

export interface CardDirectory extends SnapshotDirectory {
  values(): AsyncIterable<CardEntryHandle>;
  getFileHandle(name: string, options?: { create?: boolean }): Promise<CardFileHandle>;
}

export type DirectoryPicker = (options?: {
  mode?: 'read' | 'readwrite';
  id?: string;
}) => Promise<SnapshotDirectory>;

export type PickResult =
  | { kind: 'picked'; directory: SnapshotDirectory }
  | { kind: 'cancelled' }
  | { kind: 'unsupported' };

import {
  decideMark,
  isCardRoot,
  MARK_NAME,
  parseCardMark,
  scopeFor,
  serialiseCardMark,
} from '../card/mark';
import { UNSCOPED } from '../card/session';
import { putCoverRecord } from '../storage/media';
import { parseCoverArchive } from '../storage/coverArchive';

/** The archive's name at the card root, beside k7.db, k7.card and k7.select. */
export const COVERS_NAME = 'k7.covers';

/**
 * Seed the content-addressed cover store from the card's `k7.covers`, when it has one.
 *
 * A card that has been through a scan carries the preprocessed art for every distinct cover,
 * so a browser that does not have the cache (a new one, or this one after its storage was
 * cleared) can take it from the card instead of decoding and encoding every picture again.
 * Best effort: a missing, older or damaged archive is not a reason to refuse the card.
 */
export async function restoreCoversFromCard(directory: CardDirectory): Promise<number> {
  try {
    const handle = await directory.getFileHandle(COVERS_NAME);
    const file = await handle.getFile();
    const covers = parseCoverArchive(new Uint8Array(await file.arrayBuffer()));
    for (const cover of covers) {
      await putCoverRecord(cover.hash, {
        bytes: cover.bytes,
        format: cover.format,
        width: cover.width,
        height: cover.height,
      });
    }
    return covers.length;
  } catch {
    return 0;
  }
}

/**
 * A card root whose mark is absent-and-unwritable, foreign or newer: identity falls back to
 * the fingerprint of its contents (card-mark.md G1). Distinct from `UNSCOPED`, which is a
 * Folder — measured like anything else, and never given a card's scope.
 */
export const UNMARKED_CARD = 'unmarked-card';

/** The browser's picker, or undefined where there is none. The returned function asks
 * for read-write: a card the page cannot write back to is a card the snapshot has to be
 * dragged onto by hand. */
export function cardPicker(): DirectoryPicker | undefined {
  const picker = (globalThis as { showDirectoryPicker?: DirectoryPicker }).showDirectoryPicker;
  if (typeof picker !== 'function') return undefined;
  return (options) => picker({ mode: 'readwrite', id: 'k7-card', ...options });
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
  name = 'k7.db',
): Promise<void> {
  const handle = await directory.getFileHandle(name, { create: true });
  const writable = await handle.createWritable();
  await writable.write(bytes);
  await writable.close();
}

/**
 * Identify the picked directory and return the **Scope** its work belongs to
 * (card-mark.md §4).
 *
 * A directory without `music/` is a Folder: measured like anything else and never written
 * to, so it gets `UNSCOPED`. A card is marked once, here, at the pick — the run needs a
 * scope before it starts writing covers, and the pick is already the explicit "prepare this
 * card" act. A mark this App did not write is never overwritten: a foreign or newer file
 * means the caller falls back to the card's fingerprint.
 */
export async function identifyCard(directory: CardDirectory): Promise<string> {
  const names: string[] = [];
  try {
    for await (const child of directory.values()) names.push(child.name);
  } catch {
    return UNSCOPED;
  }
  if (!isCardRoot(names)) return UNSCOPED;

  // The covers are content-addressed and independent of the mark; take them before the run
  // can ask for one. A card whose archive is absent or damaged just keeps the browser's own.
  await restoreCoversFromCard(directory);

  let text = '';
  try {
    const handle = await directory.getFileHandle(MARK_NAME);
    text = await (await handle.getFile()).text();
  } catch {
    /* No mark yet is the ordinary first visit, not a failure. */
  }

  const state = parseCardMark(text);
  const decision = decideMark(state, { writable: true });
  if (decision.action === 'use' && typeof state === 'object') return scopeFor(state);
  if (decision.action === 'create' && decision.write !== undefined) {
    try {
      await writeSnapshotToCard(
        directory,
        new TextEncoder().encode(serialiseCardMark(decision.write)),
        MARK_NAME,
      );
      return scopeFor(decision.write);
    } catch {
      /* A card that refused the write is still a card: the caller fingerprints it. */
      return UNMARKED_CARD;
    }
  }
  return UNMARKED_CARD;
}
