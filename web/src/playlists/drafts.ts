import { useSyncExternalStore } from 'react';
import { openDB, type DBSchema, type IDBPDatabase } from 'idb';
import { playlistId } from '../domain/snapshot';
import { currentCardScope, subscribeCardScope, UNSCOPED } from '../card/session';
import { newId } from '../lib/id';
import type { ListRecord } from './lists';

/**
 * A Playlist whose last edit has not reached its own card yet: this browser's copy while the
 * file is being written, or while the card is not here to take it (ADR 0095).
 *
 * A Playlist row is a path below a card's `music/`, so a Draft belongs to the CARD whose
 * tracks it names, not to the user globally (ADR 0083). Drafts are keyed by the same
 * **Scope** every card-level store uses (`card/session.ts`): `mark:<id>`,
 * `fingerprint:<sha256>`, or `local` when no card is identified. The in-memory cache, and
 * every reader and writer, operate on the CURRENT card's drafts; a pick that changes the
 * card reloads them.
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

/**
 * The row a draft is stored as: the Draft plus the card scope it belongs to. The scope is a
 * storage concern, so `normalise` drops it when the row is read back into memory.
 */
type StoredDraft = Omit<Draft, 'entries'> & { scope: string; entries?: DraftEntry[] };

/** A draft as v1 stored it: a list of paths and nothing else, with no card scope. */
type LegacyDraft = Omit<Draft, 'entries'> & { entries?: DraftEntry[] };

/**
 * A **pending removal**: a list deleted here that its card still carries. It is stored under the
 * same scope as the drafts, because a removal belongs to the card that has to be written, and it
 * is what keeps a deleted list from coming back when the card is read again (ADR 0092).
 */
interface RemovedRecord {
  scope: string;
  id: string;
  /** Unix seconds, when it was deleted here. */
  at: number;
}

interface DraftsDB extends DBSchema {
  drafts: {
    /** `[scope, id]`: two cards may each author a list with the same id. */
    key: [string, string];
    value: StoredDraft;
    indexes: { scope: string };
  };
  removed: {
    /** `[scope, id]`, the id of a list this browser deleted for that card. */
    key: [string, string];
    value: RemovedRecord;
    indexes: { scope: string };
  };
}

const DB_NAME = 'k7-playlists';
const STORE = 'drafts';
const REMOVED = 'removed';
const DB_VERSION = 3;

let db: Promise<IDBPDatabase<DraftsDB>> | null = null;

function database(): Promise<IDBPDatabase<DraftsDB>> {
  db ??= openDB<DraftsDB>(DB_NAME, DB_VERSION, {
    upgrade: async (upgradeDb, oldVersion, _newVersion, transaction) => {
      if (oldVersion < 2) {
        // v1 held every draft as one global list, keyed by `id` and scoped to nothing. A
        // draft is authored content, not a measurement, so it is kept rather than discarded
        // the way a media row is: it had no card, and the honest home for "no card" is
        // `local`.
        let legacy: LegacyDraft[] = [];
        if (upgradeDb.objectStoreNames.contains(STORE)) {
          legacy = (await transaction.objectStore(STORE).getAll()) as LegacyDraft[];
          upgradeDb.deleteObjectStore(STORE);
        }
        const store = upgradeDb.createObjectStore(STORE, { keyPath: ['scope', 'id'] });
        store.createIndex('scope', 'scope');
        for (const draft of legacy) await store.put({ ...draft, scope: UNSCOPED });
      }
      if (oldVersion < 3) {
        const store = upgradeDb.createObjectStore(REMOVED, { keyPath: ['scope', 'id'] });
        store.createIndex('scope', 'scope');
      }
    },
  });
  return db;
}

let cache: Draft[] = [];
/** The current card's pending removals, in memory beside its drafts. */
let removed: string[] = [];
let loaded = false;
const listeners = new Set<() => void>();

function emit(): void {
  for (const listener of listeners) listener();
}

/**
 * The card the lists on screen belong to: the **Device card**'s mark when that card is the
 * selected source, the card in the computer otherwise. The pages set it from the same selection
 * the Playlists area follows, and the store reads and writes under it — a pending edit must be
 * filed under the card it belongs to, not under the card this browser last worked on
 * (ADR 0095).
 *
 * It starts as the browser's card so that a page which has not set it yet behaves as before.
 */
let scope = currentCardScope();

/** Follow the browser's card while nothing has chosen a list scope of its own. */
let followsCard = true;

export function listScope(): string {
  return scope;
}

/**
 * Work on another card's lists. Called by the pages when the selected source changes; the
 * store reloads under the new scope, and a read a later change has superseded is dropped.
 */
export function setListScope(next: string): void {
  if (next === scope) return;
  scope = next;
  followsCard = next === currentCardScope();
  void loadDrafts();
}

// A pick in the computer moves the lists with it, unless a page is showing another card's.
subscribeCardScope(() => {
  if (!followsCard) return;
  scope = currentCardScope();
  void loadDrafts();
});

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
  // rather than at send time. `cache` is one card's drafts, which is also one image's.
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

/** Read a stored row back as a Draft: track blocks when `entries` is absent, and the stored
 * scope dropped, because a Draft in memory is the current card's and carries no scope. */
function normalise(stored: StoredDraft): Draft {
  if (Array.isArray(stored.entries) && stored.entries.length > 0) {
    return {
      id: stored.id,
      name: stored.name,
      createdAt: stored.createdAt,
      entries: stored.entries,
      rows: rowsOf(stored.entries),
    };
  }
  const entries = stored.rows.map(trackEntry);
  return {
    id: stored.id,
    name: stored.name,
    createdAt: stored.createdAt,
    entries,
    rows: [...stored.rows],
  };
}

/** The drafts the current card holds in this browser, newest last. The order is the list's own. */
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

/**
 * Read the current card's drafts, at boot or whenever the scope changes. Safe to call again
 * mid-flight: a read a later scope change has superseded is dropped rather than letting the
 * previous card's rows land under the new one.
 */
export async function loadDrafts(): Promise<void> {
  const wanted = scope;
  cache = [];
  removed = [];
  loaded = false;
  emit();
  try {
    const store = await database();
    const rows = (await store.getAllFromIndex(STORE, 'scope', wanted)) as StoredDraft[];
    const removals = (await store.getAllFromIndex(REMOVED, 'scope', wanted)) as RemovedRecord[];
    if (scope !== wanted) return;
    cache = rows.map(normalise).sort((left, right) => left.createdAt - right.createdAt);
    removed = removals.map((record) => record.id).sort();
    loaded = true;
  } catch {
    if (scope !== wanted) return;
    cache = [];
    removed = [];
    loaded = true;
  }
  emit();
}

async function persist(draft: StoredDraft): Promise<void> {
  try {
    await (await database()).put(STORE, draft);
  } catch {
    /* The draft stays in memory; losing it on reload is the honest outcome of a full disk. */
  }
}

async function remove(key: [string, string]): Promise<void> {
  try {
    await (await database()).delete(STORE, key);
  } catch {
    /* The draft is already gone from memory; the delete was best-effort. */
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
  void persist({ ...draft, scope });
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
  void persist({ ...next, scope });
  emit();
  return next;
}

export function deleteDraft(id: string): void {
  const key = hexId(id);
  cache = cache.filter((draft) => draft.id !== key);
  void remove([scope, key]);
  emit();
}

/**
 * Read a card's list into this browser's drafts, keeping its identity (ADR 0092).
 *
 * A card can carry a list this browser has never seen — made on another computer, or before
 * this browser's storage was cleared — and opening it must not fork it into a second list with a
 * new id: the id is what the device selects and what the image's list record carries. A card's
 * list is flat, so an adopted list is one block per path; the folder structure the editor
 * remembers is this browser's own and is not on the card to bring back.
 */
export function adoptDraft(list: ListRecord): Draft {
  const key = hexId(list.id);
  const existing = cache.find((draft) => draft.id === key);
  if (existing !== undefined) {
    /* The list is here after all: nothing to adopt, and nothing left to remove. */
    clearRemovals([key]);
    return existing;
  }
  const entries = list.rows.map(trackEntry);
  const draft: Draft = {
    id: key,
    name: list.name,
    createdAt: list.createdAt,
    entries,
    rows: rowsOf(entries),
  };
  cache = [...cache, draft];
  void persist({ ...draft, scope });
  clearRemovals([key]);
  emit();
  return draft;
}

/**
 * Delete one list: from this browser now, and from its card the next time that card is written.
 *
 * The card is the store, so a deletion that only forgot this browser's copy would bring the list
 * back on the next read. Until a write reaches the card the id stays a **pending removal**, and
 * the page says so rather than claiming the card is already without it.
 */
export function removeList(id: string): void {
  const key = hexId(id);
  const card = scope;
  cache = cache.filter((draft) => draft.id !== key);
  void remove([card, key]);
  if (!removed.includes(key)) {
    removed = [...removed, key].sort();
    void persistRemoval(card, key);
  }
  emit();
}

/** The current card's pending removals: ids deleted here that its card has not been written since. */
export function listRemovals(): string[] {
  return removed;
}

/** …re-rendered on every change and every card change, beside `useDrafts`. */
export function useRemovals(): string[] {
  return useSyncExternalStore(subscribeDrafts, listRemovals, listRemovals);
}

/**
 * The card has taken the removals: a write reached it, or its lists were read and do not carry
 * them. With no ids, every removal for the current card is dropped.
 */
export function clearRemovals(ids?: readonly string[]): void {
  const card = scope;
  const target = new Set(ids === undefined ? removed : ids.map(hexId));
  const gone = removed.filter((id) => target.has(id));
  if (gone.length === 0) return;
  removed = removed.filter((id) => !target.has(id));
  for (const id of gone) void forgetRemoval(card, id);
  emit();
}

async function persistRemoval(scope: string, id: string): Promise<void> {
  try {
    await (await database()).put(REMOVED, { scope, id, at: Math.floor(Date.now() / 1000) });
  } catch {
    /* The removal stays in memory; losing it on reload is the honest outcome of a full disk. */
  }
}

async function forgetRemoval(scope: string, id: string): Promise<void> {
  try {
    await (await database()).delete(REMOVED, [scope, id]);
  } catch {
    /* Already gone. */
  }
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

/** The id a fresh draft with this name would get, without creating it. */
export function previewId(name: string): string {
  return freshId(name, Math.floor(Date.now() / 1000));
}

/** The current card's drafts, re-rendered on every change and every card change. */
export function useDrafts(): Draft[] {
  return useSyncExternalStore(subscribeDrafts, listDrafts, listDrafts);
}

/**
 * The draft an address names, when this card has it: an id written without its leading zeros
 * still names the same list, and anything that is not an id names nothing rather than throwing.
 */
export function findDraft(drafts: readonly Draft[], id: string): Draft | undefined {
  const wanted = id.trim().toLowerCase().replace(/^0x/, '');
  if (!/^[0-9a-f]{1,16}$/.test(wanted)) return undefined;
  const key = wanted.padStart(16, '0');
  return drafts.find((draft) => draft.id === key);
}
