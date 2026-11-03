import { ANALYZER_VERSION } from './domain/version';

/** The AGPL-3.0 source offer (ADR 0006). A public origin makes it a deployment requirement. */
export const SOURCE_URL = 'https://github.com/xperiments/k7';

/**
 * The device's one address (bridge-protocol.md X3, k7-app.md L3). The Launcher is the only
 * page on it, and it is the only origin the App trusts: an IP, another port and another host
 * are all rejected. A device link is built from this constant, never from `location`.
 */
export const DEVICE_ORIGIN = 'http://k7.local';

/** The window names of bridge-protocol.md §1. `window.open` reuses a named window. */
export const DEVICE_WINDOW = 'k7-bridge';
export const APP_WINDOW = 'k7-app';

export function cacheKey(hash: string, modelSet: string): string {
  return `${hash}:${ANALYZER_VERSION}:${modelSet}`;
}
