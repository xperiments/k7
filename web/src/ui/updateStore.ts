/**
 * The waiting service worker, as one fact the shell can render.
 *
 * `watchForUpdate` needs the registration the page made at boot, and the banner needs to
 * know when it changed. Both live here so `main.tsx` can start the watch before React
 * mounts and `UpdateBanner` can subscribe to it.
 */

import { applyUpdate, watchForUpdate, type RegistrationLike } from '../update';

let ready = false;
let registration: RegistrationLike | null = null;
let stop: (() => void) | null = null;
const listeners = new Set<() => void>();

function notify(): void {
  for (const listener of listeners) listener();
}

/** Safe to call twice: a hot reload must not stack listeners on one registration. */
export function startUpdateWatch(next: RegistrationLike): void {
  registration = next;
  if (stop !== null) return;
  stop = watchForUpdate(next, () => {
    ready = true;
    notify();
  });
}

export function updateReady(): boolean {
  return ready;
}

export function subscribeUpdate(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/** Posts SKIP_WAITING and reloads; `applyUpdate` owns both, once (O6). */
export function applyUpdateNow(): void {
  if (registration !== null) applyUpdate(registration);
}
