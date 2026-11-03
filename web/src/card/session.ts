/**
 * Which card this session is working on.
 *
 * A **Scope** is the key every card-level store is written under (card-mark.md §2): covers,
 * the Last image and a run's own state belong to one card, and the analysis store does not
 * — an analysis is a fact about bytes and stays portable.
 *
 * The scope is set once, when the card is identified at the pick, and read at every store
 * call afterwards. `UNSCOPED` is what an unidentified directory gets: a single bucket, so a
 * Folder's covers cannot pretend to belong to a card, and nothing is written under a name
 * the App invented.
 */

import type { SnapshotCard } from '../storage/snapshots';

/** The scope of work on a directory the App could not identify as a card. */
export const UNSCOPED = 'local';

const MARK_PREFIX = 'mark:';
const FINGERPRINT_PREFIX = 'fingerprint:';

let scope = UNSCOPED;
const listeners = new Set<() => void>();

export function setCardScope(next: string): void {
  if (next === scope) return;
  scope = next;
  for (const listener of listeners) listener();
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
