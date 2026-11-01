import { useSyncExternalStore } from 'react';
import { openDB, type IDBPDatabase } from 'idb';
import { playlistId, type SnapshotPlaylist } from '../domain/snapshot';

/**
 * A Playlist before it has been sent: this browser's copy, which becomes the device's
 * copy only when an image carrying it is accepted (ADR 0051, and the front door spec's
 * Playlists area).
 *
 * A draft lives in IndexedDB beside the covers, because the page has nowhere else to put
 * one: the device holds what the last image said, and the card is not writable from the
 * page. Its rows are **Snapshot paths** — the library's path below `music/`, which is
 * what a list row names on the device.
 */
export interface Draft {
  /** 16 hex characters: the opaque id the Snapshot's list record carries. */
  id: string;
  name: string;
  /** Unix seconds. */
  createdAt: number;
  /** Snapshot paths, in the order they should play. A path may repeat. */
  rows: string[];
}

const DB_NAME = 'xtape-playlists';
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
    const rows = (await (await database()).getAll(STORE)) as Draft[];
    cache = rows.sort((left, right) => left.createdAt - right.createdAt);
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
  const draft: Draft = { id: freshId(name, createdAt), name, createdAt, rows: [...rows] };
  cache = [...cache, draft];
  void persist(draft);
  emit();
  return draft;
}

export function updateDraft(
  id: string,
  patch: Partial<Pick<Draft, 'name' | 'rows'>>,
): Draft | null {
  const key = hexId(id);
  const index = cache.findIndex((draft) => draft.id === key);
  if (index === -1) return null;
  const next = { ...cache[index], ...patch };
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

/** Add rows to the end of a draft, keeping the authored order and allowing repeats. */
export function addRows(id: string, paths: string[]): Draft | null {
  const draft = getDraft(id);
  if (!draft) return null;
  return updateDraft(id, { rows: [...draft.rows, ...paths] });
}

export function moveRow(id: string, from: number, to: number): Draft | null {
  const draft = getDraft(id);
  if (!draft) return null;
  if (from < 0 || from >= draft.rows.length || to < 0 || to >= draft.rows.length) return null;
  const rows = [...draft.rows];
  const [row] = rows.splice(from, 1);
  rows.splice(to, 0, row);
  return updateDraft(id, { rows });
}

export function removeRow(id: string, index: number): Draft | null {
  const draft = getDraft(id);
  if (!draft) return null;
  return updateDraft(id, { rows: draft.rows.filter((_, at) => at !== index) });
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
