import { useEffect, useSyncExternalStore } from 'react';

import { listVirtualCards, type VirtualCard } from '../../storage/vcards';

/**
 * The virtual cards this browser remembers, shared by everything that shows them.
 *
 * The shelf in Prepare and the bar in the shell are two views of one list: the bar must not
 * wait for a route change to learn that a card was created, renamed or removed, and the shelf
 * must not read the store a second time. The list is small and changes only on an explicit
 * act, so one module-level snapshot and a subscription is enough.
 */

export interface VirtualCardsState {
  cards: VirtualCard[];
  /** A read has answered — successfully or not. Until then the views show their skeletons. */
  loaded: boolean;
  /** The read's failure, so the bar can say so and offer Retry. */
  error: string | null;
}

const EMPTY: VirtualCardsState = { cards: [], loaded: false, error: null };

let state: VirtualCardsState = EMPTY;
let inFlight: Promise<void> | null = null;
const listeners = new Set<() => void>();

function publish(next: VirtualCardsState): void {
  state = next;
  for (const listener of listeners) listener();
}

function subscribe(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

function snapshot(): VirtualCardsState {
  return state;
}

/** Read the list again. Calls made while a read is in flight join that read. */
export async function refreshVirtualCards(): Promise<void> {
  if (inFlight !== null) return inFlight;
  inFlight = (async () => {
    try {
      const cards = await listVirtualCards();
      publish({ cards, loaded: true, error: null });
    } catch (error) {
      publish({
        cards: state.cards,
        loaded: true,
        error: error instanceof Error ? error.message : String(error),
      });
    } finally {
      inFlight = null;
    }
  })();
  return inFlight;
}

/** Remember a card the caller just created or renamed, in the order the store lists. */
export function rememberVirtualCard(card: VirtualCard): void {
  const exists = state.cards.some((each) => each.id === card.id);
  publish({
    cards: exists
      ? state.cards.map((each) => (each.id === card.id ? card : each))
      : [...state.cards, card],
    loaded: true,
    error: null,
  });
}

/** Forget a card the caller just removed. */
export function forgetVirtualCard(id: string): void {
  publish({ ...state, cards: state.cards.filter((each) => each.id !== id), loaded: true });
}

/** The shared list, read on first use. */
export function useVirtualCards(): VirtualCardsState {
  const value = useSyncExternalStore(subscribe, snapshot, snapshot);
  useEffect(() => {
    if (!value.loaded) void refreshVirtualCards();
  }, [value.loaded]);
  return value;
}
