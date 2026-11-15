import { cardHandleFor, ensureRead, queryAccess } from '../card/handles';
import {
  encodeList,
  listIdOf,
  listFingerprint,
  readList,
  type ListFile,
} from './file';
import type { ListRecord } from './lists';

/**
 * The lists a card carries, read and written through its directory.
 *
 * A **Playlist** is a file under `k7.playlists/` (ADR 0095), so the card answers for itself:
 * with the card in the computer its files are read and written here; with the card in the
 * player the device answers instead (`library/lists`'s source rule). There is no copy of a
 * card's lists in this browser — a card that is nowhere has no readable lists, and the page
 * says so rather than showing something dated (ADR 0095).
 *
 * Every read validates the file with `file.ts`, the same rules the device applies, and a file
 * that fails them is skipped: one damaged list must not hide the rest, and the page can count
 * what it could not read.
 */

export interface CardLists {
  kind: 'card';
  lists: ListRecord[];
  /** Files in the directory that are not lists this build reads. */
  unreadable: number;
}

/** The directory's entries. The DOM lib this project compiles against has no `values()`. */
interface DirEntry {
  kind: 'file' | 'directory';
  name: string;
  text(): Promise<string>;
}

function entriesOf(directory: FileSystemDirectoryHandle): AsyncIterable<DirEntry> {
  const walker = directory as unknown as { values(): AsyncIterable<DirEntry> };
  return walker.values();
}

function toRecord(list: ListFile): ListRecord {
  return {
    id: list.id,
    name: list.name,
    createdAt: list.createdAt,
    rows: list.rows,
    fingerprint: listFingerprint(list),
  };
}

/** The card root as something this module can walk, or null when it cannot be read. */
async function readableRoot(scope: string, ask: boolean): Promise<FileSystemDirectoryHandle | null> {
  const handle = await cardHandleFor(scope).catch(() => null);
  if (handle === null) return null;
  const granted = ask ? await ensureRead(handle) : await queryAccess(handle, 'read');
  return granted ? handle : null;
}

async function playlistsDirectory(
  root: FileSystemDirectoryHandle,
  create: boolean,
): Promise<FileSystemDirectoryHandle | null> {
  try {
    return await root.getDirectoryHandle('k7.playlists', { create });
  } catch {
    /* A card no list has been written to has no directory: ordinary, not an error. */
    return null;
  }
}

/**
 * The lists the card carries. `ask` is for a gesture: with it, a card whose read grant lapsed
 * is asked for one; without it nothing prompts, and an unreadable card answers null.
 */
export async function readCardLists(
  scope: string,
  options: { ask?: boolean } = {},
): Promise<CardLists | null> {
  const root = await readableRoot(scope, options.ask === true);
  if (root === null) return null;
  const directory = await playlistsDirectory(root, false);
  if (directory === null) return { kind: 'card', lists: [], unreadable: 0 };

  const lists: ListRecord[] = [];
  let unreadable = 0;
  for await (const entry of entriesOf(directory)) {
    if (entry.kind !== 'file') continue;
    if (listIdOf(entry.name) === null) continue;
    try {
      lists.push(toRecord(readList(await entry.text(), entry.name)));
    } catch {
      /* A file that is not a list this build reads is skipped and counted; one damaged
       * list must not hide the rest of the card. */
      unreadable++;
    }
  }
  lists.sort((left, right) => left.id.localeCompare(right.id));
  return { kind: 'card', lists, unreadable };
}

/**
 * Write one list to its card, atomically: `createWritable` writes a temporary file and commits
 * it on close, so a card that fills mid-write keeps the list it had. False when the card cannot
 * be written — no handle, no grant, or a refused write — and the caller's pending edit stays.
 */
export async function writeListFile(scope: string, list: ListFile): Promise<boolean> {
  /* The format first: a list the device would refuse never reaches the card, and a refused
   * one changes nothing — not even by creating the directory. */
  let text: string;
  try {
    text = encodeList(list);
  } catch {
    return false;
  }
  const root = await readableRoot(scope, false);
  if (root === null) return false;
  try {
    const directory = await root.getDirectoryHandle('k7.playlists', { create: true });
    const handle = await directory.getFileHandle(`${list.id}.k7list`, { create: true });
    const writable = await handle.createWritable();
    await writable.write(text);
    await writable.close();
    return true;
  } catch {
    return false;
  }
}

/** Remove one list's file. False when the card cannot be written. */
export async function removeListFile(scope: string, id: string): Promise<boolean> {
  const root = await readableRoot(scope, false);
  if (root === null) return false;
  const directory = await playlistsDirectory(root, false);
  if (directory === null) return true;
  try {
    await directory.removeEntry(`${id}.k7list`);
    return true;
  } catch {
    return false;
  }
}
