import { openDB, type DBSchema, type IDBPDatabase } from 'idb';

/**
 * The working directory's handle, kept across reloads.
 *
 * One directory, one record: the folder every virtual card lives under. The handle is
 * structured-cloneable, so it survives storage; what will not survive is the **permission**,
 * which the page must ask for again on a gesture. Same shape as `card/handles.ts`, which
 * remembers one handle per card; this remembers the one directory the cards live under.
 */

interface VdirDB extends DBSchema {
  dir: {
    key: string;
    value: FileSystemDirectoryHandle;
  };
}

const DB_NAME = 'k7-vdir';
const DB_VERSION = 1;
const KEY = 'working';

let dbPromise: Promise<IDBPDatabase<VdirDB>> | null = null;

function database(): Promise<IDBPDatabase<VdirDB>> {
  dbPromise ??= openDB<VdirDB>(DB_NAME, DB_VERSION, {
    upgrade(db) {
      if (!db.objectStoreNames.contains('dir')) db.createObjectStore('dir');
    },
  });
  return dbPromise;
}

/** Remember the folder a person granted for the virtual cards. */
export async function rememberWorkingDirectory(handle: FileSystemDirectoryHandle): Promise<void> {
  try {
    await (await database()).put('dir', handle, KEY);
  } catch {
    /* Storage refused (private mode, quota): the page still works for this visit. */
  }
}

/** The working directory this browser has, or null when it has never been granted. */
export async function workingDirectory(): Promise<FileSystemDirectoryHandle | null> {
  try {
    return (await (await database()).get('dir', KEY)) ?? null;
  } catch {
    return null;
  }
}

/** Forget the working directory. */
export async function forgetWorkingDirectory(): Promise<void> {
  try {
    await (await database()).delete('dir', KEY);
  } catch {
    /* Nothing to forget. */
  }
}
