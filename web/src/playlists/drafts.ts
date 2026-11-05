import { useSyncExternalStore } from 'react';
import { openDB, type IDBPDatabase } from 'idb';
import { playlistId, type SnapshotPlaylist } from '../domain/snapshot';
import { newId } from '../lib/id';

/**
 * A Playlist before it has been sent: this browser's copy, which becomes the device's
 * copy only when an image carrying it is accepted (ADR 0051, and the front door spec's
 * Playlists area).
 *
 * A draft lives in IndexedDB beside the covers, because the page has nowhere else to put
 * one: the device holds what the last image said, and the card is not writable from the
 * page. Its rows are **Snapshot paths** — the library's path below `music/`, which is
 * what a list row names on the device.
 *
 * The editor's list is **entries**: a track, or a folder carrying the tracks it was added
 * with. The folder is an authoring block, not a device idea — the Snapshot is still one
 * flat list of paths — so `rows` is always the flattening of `entries` and the store owns
 * that invariant. A draft saved before folders existed has no `entries` and is read back as
 * one track entry per row.
 */

/** One block of the editor's ordered list. */
export type DraftEntry =
  | { id: string; kind: 'track'; path: string }
  | {
      id: string;
      kind: 'folder';
      /** The folder's path below `music`; the block's identity when the tree is reused. */
      path: string;
      name: string;
      /** Every file the folder held when it was added, in tree order. */
      rows: string[];
    };

/** What a caller adds: the store gives it an id. */
export type NewEntry =
  | { kind: 'track'; path: string }
  | { kind: 'folder'; path: string; name: string; rows: string[] };

export interface Draft {
  /** 16 hex characters: the opaque id the Snapshot's list record carries. */
  id: string;
  name: string;
  /** Unix seconds. */
  createdAt: number;
  /** Snapshot paths in play order: the flattened `entries`, and what the device is sent. */
  rows: string[];
  /** The editor's blocks, in order. A folder keeps its files together across a move. */
  entries: DraftEntry[];
}

/** The row a draft saved before `entries` existed has: a list of paths and nothing else. */
type StoredDraft = Omit<Draft, 'entries'> & { entries?: DraftEntry[] };

const DB_NAME = 'k7-playlists';
const STORE = 'drafts';

let db: Promise<IDBPDatabase> | null = null;

function database(): Promise<IDBPDatabase> {
  db ??= openDB(DB_NAME, 1, {
    upgrade(upgradeDb) {
      upgradeDb.createObjectStore(STORE, { keyPath: 'id' });
    },
  });
  return db;
}

let cache: Draft[] = [];
let loaded = false;
const listeners = new Set<() => void>();

function emit(): void {
  for (const listener of listeners) listener();
}

export function hexId(id: bigint | string): string {
  if (typeof id === 'bigint') return id.toString(16).padStart(16, '0');
  const cleaned = id.trim().toLowerCase().replace(/^0x/, '');
  if (!/^[0-9a-f]{1,16}$/.test(cleaned)) throw new Error(`not a list id: ${id}`);
  return cleaned.padStart(16, '0');
}

function freshId(name: string, createdAt: number): string {
  let candidate = createdAt;
  let id = hexId(playlistId(name, candidate));
  // The device refuses an image whose list ids collide, so an id is checked here
  // rather than at send time.
  while (cache.some((draft) => draft.id === id)) {
    candidate += 1;
    id = hexId(playlistId(name, candidate));
  }
  return id;
}

/** The flat Snapshot order a set of blocks adds up to. */
function rowsOf(entries: readonly DraftEntry[]): string[] {
  const rows: string[] = [];
  for (const entry of entries) {
    if (entry.kind === 'folder') rows.push(...entry.rows);
    else rows.push(entry.path);
  }
  return rows;
}

function trackEntry(path: string): DraftEntry {
  return { id: newId(), kind: 'track', path };
}

function makeEntry(input: NewEntry): DraftEntry {
  return input.kind === 'folder'
    ? { id: newId(), kind: 'folder', path: input.path, name: input.name, rows: [...input.rows] }
    : { id: newId(), kind: 'track', path: input.path };
}

/** Read an older draft back as track blocks, and trust `entries` when it is there. */
function normalise(draft: StoredDraft): Draft {
  if (Array.isArray(draft.entries) && draft.entries.length > 0) {
    return { ...draft, entries: draft.entries, rows: rowsOf(draft.entries) };
  }
  const entries = draft.rows.map(trackEntry);
  return { ...draft, entries, rows: [...draft.rows] };
}

/** The drafts this browser holds, newest last. The order is the list's own. */
export function listDrafts(): Draft[] {
  return cache;
}

export function getDraft(id: string): Draft | undefined {
  return cache.find((draft) => draft.id === hexId(id));
}

export function subscribeDrafts(listener: () => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function draftsLoaded(): boolean {
  return loaded;
}

/** Read the store once, at boot. A store that cannot be read leaves no drafts, not an error. */
export async function loadDrafts(): Promise<void> {
  try {
    const rows = (await (await database()).getAll(STORE)) as StoredDraft[];
    cache = rows.map(normalise).sort((left, right) => left.createdAt - right.createdAt);
  } catch {
    cache = [];
  }
  loaded = true;
  emit();
}

async function persist(draft: Draft | string): Promise<void> {
  try {
    const handle = await database();
    if (typeof draft === 'string') await handle.delete(STORE, draft);
    else await handle.put(STORE, draft);
  } catch {
    /* The draft stays in memory; losing it on reload is the honest outcome of a full disk. */
  }
}

export function createDraft(name: string, rows: string[] = []): Draft {
  const createdAt = Math.floor(Date.now() / 1000);
  const entries = rows.map(trackEntry);
  const draft: Draft = {
    id: freshId(name, createdAt),
    name,
    createdAt,
    entries,
    rows: rowsOf(entries),
  };
  cache = [...cache, draft];
  void persist(draft);
  emit();
  return draft;
}

/**
 * Change a draft. `entries` is the editor's model; `rows` is always derived from it, never
 * passed in, so no caller can re-sort the list behind the author's back.
 */
export function updateDraft(
  id: string,
  patch: Partial<Pick<Draft, 'name' | 'entries'>>,
): Draft | null {
  const key = hexId(id);
  const index = cache.findIndex((draft) => draft.id === key);
  if (index === -1) return null;
  const next = { ...cache[index], ...patch };
  if (patch.entries !== undefined) next.rows = rowsOf(patch.entries);
  cache = cache.map((draft, at) => (at === index ? next : draft));
  void persist(next);
  emit();
  return next;
}

export function deleteDraft(id: string): void {
  const key = hexId(id);
  cache = cache.filter((draft) => draft.id !== key);
  void persist(key);
  emit();
}

/** Add tracks to the end of a draft, keeping the authored order and allowing repeats. */
export function addRows(id: string, paths: string[]): Draft | null {
  const draft = getDraft(id);
  if (!draft) return null;
  return updateDraft(id, { entries: [...draft.entries, ...paths.map(trackEntry)] });
}

/**
 * Insert blocks at `at`, clamped to the list. A drop onto block N inserts before it; a
 * drop past the end (`Number.MAX_SAFE_INTEGER`, the default) appends. Repeats are allowed,
 * because a playlist may play the same track twice.
 */
export function insertEntries(
  id: string,
  inputs: readonly NewEntry[],
  at = Number.MAX_SAFE_INTEGER,
): Draft | null {
  const draft = getDraft(id);
  if (!draft) return null;
  if (inputs.length === 0) return draft;
  const index = Math.max(0, Math.min(at, draft.entries.length));
  const entries = [...draft.entries];
  entries.splice(index, 0, ...inputs.map(makeEntry));
  return updateDraft(id, { entries });
}

/** Move one block — a track or a whole folder — to another position in the list. */
export function moveEntry(id: string, from: number, to: number): Draft | null {
  const draft = getDraft(id);
  if (!draft) return null;
  if (from < 0 || from >= draft.entries.length || to < 0 || to >= draft.entries.length) {
    return null;
  }
  const entries = [...draft.entries];
  const [entry] = entries.splice(from, 1);
  entries.splice(to, 0, entry);
  return updateDraft(id, { entries });
}

/** Remove one block, whether it is a track or a folder and the files it carried. */
export function removeEntry(id: string, index: number): Draft | null {
  const draft = getDraft(id);
  if (!draft) return null;
  if (index < 0 || index >= draft.entries.length) return null;
  return updateDraft(id, { entries: draft.entries.filter((_, at) => at !== index) });
}

/** Empty a draft; the list keeps its name and creation time. */
export function clearDraft(id: string): Draft | null {
  return updateDraft(id, { entries: [] });
}

/** What the image builder takes: the drafts, in the shape `buildSnapshot` wants. */
export function toSnapshotPlaylists(): SnapshotPlaylist[] {
  return cache.map((draft) => ({
    id: draft.id,
    name: draft.name,
    createdAt: draft.createdAt,
    rows: draft.rows,
  }));
}

/** The id a fresh draft with this name would get, without creating it. */
export function previewId(name: string): string {
  return freshId(name, Math.floor(Date.now() / 1000));
}

/** The drafts, re-rendered on every change. */
export function useDrafts(): Draft[] {
  return useSyncExternalStore(subscribeDrafts, listDrafts, listDrafts);
}
