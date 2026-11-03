/**
 * One install watcher for the whole page.
 *
 * `watchInstall` can only hear `beforeinstallprompt`, never read it back, so the
 * subscription has to exist before Chrome fires it — that is, at boot, not when the control
 * first renders. The store is that boot subscription and the control is a subscriber.
 *
 * `unknown` is the state before a Chromium has said anything. It is deliberately not
 * rendered as an install button: `promptInstall` would answer `unavailable`, and a button
 * that does nothing is worse than no button (A10).
 */

import {
  isDeviceCopy,
  promptInstall,
  readInstallState,
  watchInstall,
  type InstallEnv,
  type InstallState,
} from './install';

let current: InstallState = 'unknown';
let watching: InstallEnv | null = null;
let stop: (() => void) | null = null;
const listeners = new Set<() => void>();

/**
 * Safe to call more than once: a second call is a no-op, so a hot reload cannot stack
 * listeners and a second prompt cannot be captured.
 */
export function startInstallWatch(env: InstallEnv): void {
  if (stop !== null) return;
  watching = env;
  current = readInstallState(env);
  stop = watchInstall(env, (next) => {
    current = next;
    for (const listener of listeners) listener();
  });
}

export function installSnapshot(): InstallState {
  return current;
}

export function subscribeInstall(listener: () => void): () => void {
  listeners.add(listener);
  return () => {
    listeners.delete(listener);
  };
}

/** The Device copy can never install, so the control is not offered there at all. */
export function onDeviceCopy(): boolean {
  return watching !== null && isDeviceCopy(watching);
}

/** Replays the captured prompt. A refusal leaves the state to the browser's own report. */
export async function promptInstallNow(): Promise<void> {
  if (watching !== null) await promptInstall(watching);
}
