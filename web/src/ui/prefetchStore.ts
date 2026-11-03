/**
 * The 41 MB offline prep, as one fact the shell can render (O3, O4).
 *
 * It runs only when the App is installed — `display-mode: standalone` — because a visitor
 * who follows a link must not spend jsDelivr's donated bandwidth (or their own data) on a
 * download they never asked for. It is resumable by construction: `loadWeightBytes` verifies
 * and skips what is already in IndexedDB, so a second run fetches only what is missing.
 */

import { PREFETCH_BYTES, PREFETCH_WEIGHTS, prefetchWeights } from '../weights/prefetch';

export type OfflineStatus = 'idle' | 'running' | 'done' | 'error';

export interface OfflineState {
  status: OfflineStatus;
  done: number;
  total: number;
  bytesTotal: number;
  /** Shown only for a failure: a weight's own sentence. */
  message: string;
}

const STORAGE_KEY = 'k7.offline';
/** The marker is the weight set itself, not the app version: a model swap must re-download. */
const SET_KEY = `${PREFETCH_WEIGHTS.length}:${PREFETCH_BYTES}`;
const INITIAL: OfflineState = { status: 'idle', done: 0, total: 0, bytesTotal: 0, message: '' };

let state: OfflineState = INITIAL;
const listeners = new Set<() => void>();

function update(next: OfflineState): void {
  state = next;
  for (const listener of listeners) listener();
}

/** A weight set that finished once does not download again; a changed set does. */
function alreadyDone(): boolean {
  try {
    return localStorage.getItem(STORAGE_KEY) === SET_KEY;
  } catch {
    /* Storage can be denied entirely; then every session re-checks rather than re-downloads. */
    return false;
  }
}

function rememberDone(): void {
  try {
    localStorage.setItem(STORAGE_KEY, SET_KEY);
  } catch {
    /* The bytes are in IndexedDB either way; only the shortcut is lost. */
  }
}

export async function prepareOffline(force = false): Promise<void> {
  if (state.status === 'running' || state.status === 'done') return;
  if (!force && alreadyDone()) {
    update({ ...INITIAL, status: 'done' });
    return;
  }

  update({ ...INITIAL, status: 'running' });
  try {
    const done = await prefetchWeights({
      onProgress: (progress) => {
        update({
          status: 'running',
          done: progress.done,
          total: progress.total,
          bytesTotal: progress.bytesTotal,
          message: '',
        });
      },
    });
    rememberDone();
    update({ status: 'done', done: done.done, total: done.total, bytesTotal: done.bytesTotal, message: '' });
  } catch (error) {
    update({
      ...state,
      status: 'error',
      message: error instanceof Error ? error.message : String(error),
    });
  }
}

export function offlineSnapshot(): OfflineState {
  return state;
}

export function subscribeOffline(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}
