import { openDB, type DBSchema, type IDBPDatabase } from 'idb';

/**
 * The card's directory handle, kept across reloads.
 *
 * A handle is structured-cloneable, so the browser can remember the folder a person chose;
 * what it will not remember is the **permission**, which a page must ask for again after a
 * reload, on a gesture. That is the whole shape of this module: the handle is restored at
 * load so the page knows which card it is talking about, and `ensureRead` asks for the grant
 * at the moment someone presses play or rescan — never in the background, where a prompt
 * would be refused without a person to answer it.
 *
 * It is keyed by **Scope**, like the other card stores (`card-mark.md` §2): one card's handle
 * must never be used to read another's paths.
 */

interface HandleRecord {
  scope: string;
  handle: FileSystemDirectoryHandle;
  /** When the card was opened; for saying which card the handle belongs to. */
  at: string;
}

interface HandleDB extends DBSchema {
  handles: {
    key: string;
    value: HandleRecord;
  };
}

const DB_NAME = 'k7-handles';

/**
 * The store, opened on first use rather than at import: a page that never picks a card — and
 * a test that never touches storage — must not touch IndexedDB for this.
 */
let dbPromise: Promise<IDBPDatabase<HandleDB>> | null = null;

function handles(): Promise<IDBPDatabase<HandleDB>> {
  dbPromise ??= openDB<HandleDB>(DB_NAME, 1, {
    upgrade(db) {
      if (!db.objectStoreNames.contains('handles')) {
        db.createObjectStore('handles', { keyPath: 'scope' });
      }
    },
  });
  return dbPromise;
}

/** Remember the folder a person picked for this card. A browser without pickers never calls this. */
export async function rememberCardHandle(
  scope: string,
  handle: FileSystemDirectoryHandle,
): Promise<void> {
  try {
    await (await handles()).put('handles', {
      scope,
      handle,
      at: new Date().toISOString(),
    });
  } catch {
    /* Storage refused (private mode, quota): the page still works for this visit. */
  }
}

/** The handle this browser has for a card, or null when it has never opened it read-write. */
export async function cardHandleFor(scope: string): Promise<FileSystemDirectoryHandle | null> {
  try {
    const record = await (await handles()).get('handles', scope);
    return record?.handle ?? null;
  } catch {
    return null;
  }
}

export async function forgetCardHandle(scope: string): Promise<void> {
  try {
    await (await handles()).delete('handles', scope);
  } catch {
    /* Nothing to forget. */
  }
}

/**
 * The permission API a `FileSystemDirectoryHandle` carries but the DOM types in line with
 * this project do not declare. Absent means a test's stand-in, or a browser that grants on
 * the read itself — both are allowed to proceed.
 */
interface Permissioned {
  queryPermission?(descriptor: { mode: PermissionMode }): Promise<PermissionState>;
  requestPermission?(descriptor: { mode: PermissionMode }): Promise<PermissionState>;
}

export type PermissionMode = 'read' | 'readwrite';

/**
 * Ask for access, and say whether it was given. Must be called from a gesture: the browser
 * only shows the prompt when a person's click is the reason.
 */
export async function ensureAccess(
  handle: FileSystemDirectoryHandle,
  mode: PermissionMode,
): Promise<boolean> {
  const permissioned = handle as unknown as Permissioned;
  if (typeof permissioned.queryPermission !== 'function') return true;

  const state = await permissioned.queryPermission({ mode });
  if (state === 'granted') return true;
  if (typeof permissioned.requestPermission !== 'function') return false;
  return (await permissioned.requestPermission({ mode })) === 'granted';
}

/**
 * Whether the grant is **already** there, without asking for it. A prompt needs a person's
 * click, so a caller that is not a gesture may ask this to decide whether it can read.
 */
export async function queryAccess(
  handle: FileSystemDirectoryHandle,
  mode: PermissionMode,
): Promise<boolean> {
  const permissioned = handle as unknown as Permissioned;
  if (typeof permissioned.queryPermission !== 'function') return true;
  try {
    return (await permissioned.queryPermission({ mode })) === 'granted';
  } catch {
    return false;
  }
}

/** Reading a card's files: what playing a track off it needs, and nothing more. */
export function ensureRead(handle: FileSystemDirectoryHandle): Promise<boolean> {
  return ensureAccess(handle, 'read');
}
