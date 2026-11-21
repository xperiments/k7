/**
 * Which card this session is working on.
 *
 * A **Scope** is the key every card-level store is written under (card-mark.md §2): covers,
 * the Last image and a run's own state belong to one card, and the analysis store does not
 * — an analysis is a fact about bytes and stays portable.
 *
 * The scope is set when the card is identified at the pick, and read at every store call
 * afterwards. `UNSCOPED` is what an unidentified directory gets: a single bucket, so a
 * Folder's covers cannot pretend to belong to a card, and nothing is written under a name
 * the App invented.
 *
 * It is also **remembered in this browser**, because the pick is not something a later visit
 * can repeat: once the card is in the player, nothing in the App can read its mark, and a
 * scope that only lived in memory made every store it keyed invisible on the next page load
 * — the covers, and the Last image the Playlists area sends a list change on. What is
 * restored is "the card this browser last worked on", which is what the views already claim
 * to show (`k7-app.md` §3); a card with a different mark replaces it at the next pick.
 */

import { useSyncExternalStore } from 'react';

import type { SnapshotCard, StoredSnapshot } from '../storage/snapshots';

/** The scope of work on a directory the App could not identify as a card. */
export const UNSCOPED = 'local';

const MARK_PREFIX = 'mark:';
const FINGERPRINT_PREFIX = 'fingerprint:';

/** Where the last scope is kept: `localStorage`, beside the Settings (ADR 0053). */
const STORAGE_KEY = 'k7.scope.v1';

/**
 * A stored scope is adopted only in the two shapes this module produces. Anything else —
 * a hand-edited key, a prefix a later version invented — is not a card this build can name,
 * and guessing would point the stores at rows that belong to nothing.
 */
function parseScope(raw: string | null): string | null {
  if (!raw) return null;
  if (raw.startsWith(MARK_PREFIX) && raw.length > MARK_PREFIX.length) return raw;
  if (raw.startsWith(FINGERPRINT_PREFIX) && raw.length > FINGERPRINT_PREFIX.length) return raw;
  if (raw === UNSCOPED) return raw;
  return null;
}

function readStoredScope(): string | null {
  try {
    return parseScope(localStorage.getItem(STORAGE_KEY));
  } catch {
    // A browser that refuses storage is the stand-alone case: this visit still works, and
    // the next one starts from the recovery read below.
    return null;
  }
}

function writeStoredScope(next: string): void {
  try {
    localStorage.setItem(STORAGE_KEY, next);
  } catch {
    /* The scope still holds for this page's life; the stores stay reachable until reload. */
  }
}

/** The Device view is a preference, remembered across reloads beside the scope. */
const DEVICE_VIEW_KEY = 'k7.device-view.v1';

function readStoredDeviceView(): boolean {
  try {
    return localStorage.getItem(DEVICE_VIEW_KEY) === '1';
  } catch {
    // A browser that refuses storage still has the view for this page's life.
    return false;
  }
}

function writeStoredDeviceView(next: boolean): void {
  try {
    localStorage.setItem(DEVICE_VIEW_KEY, next ? '1' : '0');
  } catch {
    /* The view still holds for this page's life. */
  }
}

/** What the browser had stored at load, `null` when it had nothing usable. */
const stored = readStoredScope();
let scope = stored ?? UNSCOPED;
const listeners = new Set<() => void>();

/**
 * Select a card in the computer. Picking one leaves the player's card behind, so the Device
 * view is cleared and remembered as off; `preserveDeviceView` is for the boot recovery,
 * which adopts a scope without claiming a local card was picked.
 */
export function setCardScope(
  next: string,
  options: { preserveDeviceView?: boolean } = {},
): void {
  const keepDevice = options.preserveDeviceView === true;
  const changed = next !== scope || (!keepDevice && deviceView);
  scope = next;
  if (!keepDevice && deviceView) {
    deviceView = false;
    writeStoredDeviceView(false);
  }
  writeStoredScope(next);
  if (changed) {
    for (const listener of listeners) listener();
  }
}

/**
 * The card in the player is a card the App can only *view*: the ESP32-S3 holds it, the
 * browser never has its directory. Selecting it is therefore a view of the device, not a
 * scope the stores are written under — the card's own scope names its rows, and this flag
 * only says which card the Albums page should read.
 */
let deviceView = readStoredDeviceView();

export function setDeviceView(next: boolean): void {
  if (next === deviceView) return;
  deviceView = next;
  writeStoredDeviceView(next);
  for (const listener of listeners) listener();
}

/**
 * Whether this browser has ever *chosen* a view, as opposed to holding the default. The App
 * selects the player's card the first time it sees one, and must not do that to someone who
 * has already said which card they are looking at.
 */
export function deviceViewChosen(): boolean {
  try {
    return localStorage.getItem(DEVICE_VIEW_KEY) !== null;
  } catch {
    /* No storage means no way to know a choice was made: leave the view alone. */
    return true;
  }
}

export function currentDeviceView(): boolean {
  return deviceView;
}

export function useDeviceView(): boolean {
  return useSyncExternalStore(subscribeCardScope, currentDeviceView, currentDeviceView);
}

/**
 * The card this browser last worked on, when nothing recorded it.
 *
 * Scopes were not remembered before ADR 0063, so a browser that has already measured a card
 * holds its covers and its Last image under a scope nothing now names. The newest image is
 * that name — a run only produces one — and it is read once, at boot, and only when no scope
 * was stored. A pick that landed first wins: the recovery never overwrites a live choice.
 */
export async function restoreCardScope(): Promise<string> {
  if (stored !== null || scope !== UNSCOPED) return scope;
  let newest: StoredSnapshot | undefined;
  try {
    // Deferred, not a bundling trick: `storage/snapshots` opens IndexedDB as it loads, and
    // this module is also loaded where there is none — the card writer's tests, and any
    // look at the scope before the store is touched. Only the recovery read needs it.
    const { newestSnapshot } = await import('../storage/snapshots');
    newest = await newestSnapshot();
  } catch {
    /* A store that cannot be read leaves the scope as it was. */
  }
  // The read is asynchronous: a pick may have arrived while it was in flight.
  if (newest !== undefined && stored === null && scope === UNSCOPED) {
    setCardScope(newest.scope, { preserveDeviceView: true });
  }
  return scope;
}

export function currentCardScope(): string {
  return scope;
}

export function subscribeCardScope(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/**
 * The provenance a stored image records beside its scope: the raw id or the raw hex, without
 * the prefix the scope key carries (`snapshots.ts` documents the same split).
 */
export function cardRefFor(current: string = scope): SnapshotCard {
  if (current.startsWith(MARK_PREFIX)) return { mark: current.slice(MARK_PREFIX.length) };
  if (current.startsWith(FINGERPRINT_PREFIX)) {
    return { fingerprint: current.slice(FINGERPRINT_PREFIX.length) };
  }
  return {};
}

/**
 * The current scope as a React value, so a view that reads card-scoped stores re-reads them
 * when a pick changes the card. Without it a component that mounted before the pick keeps
 * showing the previous card's rows — or none at all.
 */
export function useCardScope(): string {
  return useSyncExternalStore(subscribeCardScope, currentCardScope, currentCardScope);
}
