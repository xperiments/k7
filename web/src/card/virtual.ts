/**
 * Preparing a virtual card: a card-shaped folder under a granted working directory.
 *
 * A virtual card is a Card that is not on an SD yet — `music/` plus `k7.card`, later `k7.db`
 * and `k7.covers` — living in one subfolder of the working directory. This module is the
 * policy and the operations; the two stores it writes are `storage/vcards.ts` (the shelf's
 * list) and `card/vdir.ts` (the one working-directory handle).
 *
 * The write grant does not survive a browser restart, so every act that touches the
 * directory — create, open, reconnect — starts from a gesture and asks for the grant there,
 * exactly as `card/handles.ts` does for a picked card.
 */

import { ensureAccess, rememberCardHandle } from './handles';
import { MARK_NAME, newCardMark, scopeFor, serialiseCardMark, type CardMark } from './mark';
import { setCardScope } from './session';
import { rememberWorkingDirectory, workingDirectory } from './vdir';
import { putVirtualCard, removeVirtualCard, type VirtualCard } from '../storage/vcards';

/** A byte. SD cards are sold in decimal units, so the sizes are too. */
export const GB = 1_000_000_000;

/** The standard sizes a person picks from, in gigabytes. */
export const CARD_SIZES_GB = [16, 32, 64, 128, 256] as const;

/** The minimum of a directory handle the create/open flow needs, shaped for tests. */
export interface VdirDirectory {
  name?: string;
  getDirectoryHandle(name: string, options?: { create?: boolean }): Promise<VdirDirectory>;
  getFileHandle(name: string, options?: { create?: boolean }): Promise<{
    createWritable(): Promise<{ write(data: Uint8Array): Promise<void>; close(): Promise<void> }>;
  }>;
  /** Present on a real File System Access directory; a test stand-in may omit it. */
  removeEntry?(name: string, options?: { recursive?: boolean }): Promise<void>;
}

/** The browser's picker, or undefined where there is none. Always asked read-write. */
export function workingDirectoryPicker():
  | ((options?: { id?: string }) => Promise<VdirDirectory>)
  | undefined {
  const picker = (
    globalThis as {
      showDirectoryPicker?: (options: { mode?: string; id?: string }) => Promise<VdirDirectory>;
    }
  ).showDirectoryPicker;
  return typeof picker === 'function'
    ? (options) => picker({ mode: 'readwrite', id: 'k7-vdir', ...options })
    : undefined;
}

export type Reconnect =
  | { kind: 'ready'; directory: VdirDirectory }
  | { kind: 'denied' }
  | { kind: 'cancelled' }
  | { kind: 'unsupported' }
  | { kind: 'failed'; message: string };

/**
 * Make the working directory usable now. The remembered handle is asked first — one prompt if
 * the grant is gone — and a refusal ends there rather than opening a second dialog. With no
 * remembered handle, the picker is the way in.
 */
export async function reconnectWorkingDirectory(): Promise<Reconnect> {
  const remembered = await workingDirectory();
  if (remembered !== null) {
    try {
      if (await ensureAccess(remembered, 'readwrite')) {
        return { kind: 'ready', directory: remembered as unknown as VdirDirectory };
      }
    } catch {
      /* A revoked handle reads as no grant; the denial sentence is the honest answer. */
    }
    return { kind: 'denied' };
  }

  const picker = workingDirectoryPicker();
  if (picker === undefined) return { kind: 'unsupported' };
  try {
    const directory = await picker();
    await rememberWorkingDirectory(directory as unknown as FileSystemDirectoryHandle);
    return { kind: 'ready', directory };
  } catch (error) {
    if (error instanceof DOMException && error.name === 'AbortError') return { kind: 'cancelled' };
    return { kind: 'failed', message: error instanceof Error ? error.message : String(error) };
  }
}

/** A folder name for a card: the name as a slug, plus a short id so two "Road trip" cards
 * cannot collide, and so the folder is a legal, readable file-system name. */
export function folderName(label: string, id: string): string {
  const slug =
    label
      .trim()
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-+|-+$/g, '') || 'card';
  return `${slug}-${id.slice(0, 8)}`;
}

/** The size in bytes of a chosen gigabyte count. */
export function sizeBytes(gb: number): number {
  return gb * GB;
}

/** A size in bytes as the shelf reads it: whole gigabytes, or the exact bytes for a custom one. */
export function sizeLabel(bytes: number): string {
  const gb = bytes / GB;
  return Number.isInteger(gb) ? `${gb} GB` : `${gb.toFixed(1)} GB`;
}

export interface CreatedCard {
  card: VirtualCard;
  /** The new card's own directory, for the caller that wants to open it right away. */
  directory: VdirDirectory;
}

/**
 * Create a virtual card under `directory`: make its subfolder, its `music/`, and its mark, and
 * remember it on the shelf. Nothing is measured and nothing is copied — that is the compose
 * act, a later step.
 *
 * `now` and `random` are injected for tests; production callers pass neither.
 */
export async function createVirtualCard(
  directory: VdirDirectory,
  label: string,
  capacity: number,
  options: {
    now?: string;
    random?: (bytes: Uint8Array) => Uint8Array;
  } = {},
): Promise<CreatedCard> {
  if (!Number.isFinite(capacity) || capacity <= 0) {
    throw new Error('a virtual card needs a positive capacity');
  }

  const name = label.trim();
  const mark = newCardMark(options.now ?? new Date().toISOString(), name || undefined, options.random);
  mark.capacity = capacity;

  const cardDirectory = await directory.getDirectoryHandle(folderName(name, mark.id), {
    create: true,
  });
  // A card is a directory holding `music/` (card-mark.md K1): create it so the folder is a
  // card from the first moment, not a Folder waiting to be prepared.
  await cardDirectory.getDirectoryHandle('music', { create: true });

  const markHandle = await cardDirectory.getFileHandle(MARK_NAME, { create: true });
  const writable = await markHandle.createWritable();
  await writable.write(new TextEncoder().encode(serialiseCardMark(mark)));
  await writable.close();

  const card: VirtualCard = {
    id: mark.id,
    label: name,
    folder: folderName(name, mark.id),
    sizeBytes: capacity,
    scope: scopeFor(mark),
    created: mark.created,
  };
  await putVirtualCard(card);
  return { card, directory: cardDirectory };
}

/**
 * Open a virtual card: make it the card this session works on, and remember its own directory
 * so a later read of its files can re-ask the grant on the gesture that needs it (the same
 * shape `intake.ts` uses for a picked card). Measuring its music is a later step.
 */
export async function openVirtualCard(card: VirtualCard, directory: VdirDirectory): Promise<void> {
  const scope = scopeFor({ id: card.id });
  setCardScope(scope);
  // Best effort: a handle that cannot be stored is still a usable card for this visit.
  await rememberCardHandle(scope, directory as unknown as FileSystemDirectoryHandle);
}

/**
 * Rename a virtual card. The name is the record's `label`; the card's own mark is rewritten so
 * the file and the shelf agree. The folder name is derived from the id and does not move.
 */
export async function renameVirtualCard(
  directory: VdirDirectory,
  card: VirtualCard,
  label: string,
): Promise<VirtualCard> {
  const name = label.trim();
  const cardDirectory = await directory.getDirectoryHandle(card.folder);
  const mark: CardMark = { v: 1, id: card.id, created: card.created, capacity: card.sizeBytes };
  if (name !== '') mark.label = name;
  const handle = await cardDirectory.getFileHandle(MARK_NAME, { create: true });
  const writable = await handle.createWritable();
  await writable.write(new TextEncoder().encode(serialiseCardMark(mark)));
  await writable.close();

  const next: VirtualCard = { ...card, label: name };
  await putVirtualCard(next);
  return next;
}

/**
 * Remove a virtual card: its folder (and the music composed into it — the originals the user
 * added are untouched) and its record, so it leaves the shelf. The scoped stores are left
 * behind; they are pruned by age, and a card re-created from the same mark would want them.
 */
export async function deleteVirtualCard(
  directory: VdirDirectory,
  card: VirtualCard,
): Promise<void> {
  try {
    await directory.removeEntry?.(card.folder, { recursive: true });
  } catch {
    /* A folder already gone is a card already removed. */
  }
  await removeVirtualCard(card.id);
}
