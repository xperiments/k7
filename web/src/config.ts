import { ANALYZER_VERSION } from './domain/version';

/** The AGPL-3.0 source offer (ADR 0006). A public origin makes it a deployment requirement. */
export const SOURCE_URL = 'https://github.com/xperiments/k7';

/**
 * The device's one address (k7-app.md L3), and the only origin the App trusts: an IP,
 * another port and another host are all rejected. A device request is built from this
 * constant, never from `location`.
 */
export const DEVICE_ORIGIN = 'http://k7.local';

/** The App's window name. `window.open` reuses a named window. */
export const APP_WINDOW = 'k7-app';

export function cacheKey(hash: string, modelSet: string): string {
  return `${hash}:${ANALYZER_VERSION}:${modelSet}`;
}
