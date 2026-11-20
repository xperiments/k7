import { useMemo, useSyncExternalStore } from 'react';

import { FACTORY_JCARDS } from './factory';
import { readJcardPack, type JcardDesign } from './model';
import { faceIndexFor } from './seed';

/**
 * The Cover pool (web/CONTEXT.md, art-packs.md §6): the factory J-cards plus the active user
 * pack's, factory first, each set in id order. An album with no picture draws one of these;
 * a real Cover picture always wins, so the pool is only consulted for the no-cover face.
 *
 * K7-03 ships the factory designs alone. APP-65 is the writer: its Art packs page parses the
 * active pack's J-cards (after checking the pack's header and CRC) and calls
 * {@link setUserJcards}; a reset calls {@link resetUserJcards}. The store is a
 * `useSyncExternalStore` source, so the shelf, the cards and the Player redraw when the
 * active pack changes, with no reload.
 */

let active: readonly JcardDesign[] = FACTORY_JCARDS;
let user: readonly JcardDesign[] = [];
const listeners = new Set<() => void>();

function activate(nextUser: readonly JcardDesign[]): void {
  user = nextUser.slice();
  active = [...FACTORY_JCARDS, ...user];
  for (const listener of listeners) listener();
}

/** The pool as the App draws it: the factory designs, then the active user pack's. */
export function activePool(): readonly JcardDesign[] {
  return active;
}

/** The active user pack's designs, in the pool's order after the factory's. */
export function userJcards(): readonly JcardDesign[] {
  return user;
}

/**
 * Replace the active user pack's J-cards. The factory designs are never replaced and stay
 * first; the caller has already checked the pack (art-packs.md §4).
 */
export function setUserJcards(designs: readonly JcardDesign[]): void {
  activate(designs);
}

/**
 * Parse and install a pack's J-cards (its `{ v, designs }` payload, art-packs.md §1).
 * Returns the designs installed, or null when the payload is not a pack - in which case the
 * active pool is untouched, because a bad pack changes nothing.
 */
export function setUserJcardPack(raw: unknown): JcardDesign[] | null {
  const designs = readJcardPack(raw);
  if (!designs) return null;
  activate(designs);
  return designs;
}

/** Back to the factory alone; the reset's reset (art-packs.md §4). */
export function resetUserJcards(): void {
  activate([]);
}

export function subscribePool(listener: () => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

/**
 * Pick a design for a title and artist: the pair's stable seed modulo the pool's size
 * (art-packs.md §6). The same pair always gets the same design, and adding a user pack only
 * re-deals the index, never the factory order.
 */
export function designFor(
  title: string,
  artist: string,
  pool: readonly JcardDesign[] = active,
): JcardDesign | null {
  const index = faceIndexFor(title, artist, pool.length);
  return index < 0 ? null : (pool[index] ?? null);
}

/** A pool-aware pick, exported for a caller that already holds the pool (the Art packs page). */
export function pickDesign(
  pool: readonly JcardDesign[],
  title: string,
  artist: string,
): JcardDesign | null {
  return designFor(title, artist, pool);
}

/** The pool, as a React store: redraws the faces when APP-65 installs or resets a pack. */
export function useJcardPool(): readonly JcardDesign[] {
  return useSyncExternalStore(subscribePool, activePool, activePool);
}

/** The design for one album or Track, kept while the pool is unchanged. */
export function useJcardDesign(title: string, artist: string): JcardDesign | null {
  const pool = useJcardPool();
  return useMemo(() => designFor(title, artist, pool), [title, artist, pool]);
}
