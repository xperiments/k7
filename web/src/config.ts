import { ANALYZER_VERSION } from './domain/version';

/** The AGPL-3.0 source offer (ADR 0006). A public origin makes it a deployment requirement. */
export const SOURCE_URL = 'https://github.com/xperiments/k7';

/**
 * The device's one address (k7-app.md L3), and the only origin the App trusts: an IP,
 * another port and another host are all rejected. A device request is built from this
 * constant, never from `location`.
 */
export const DEVICE_ORIGIN = 'http://k7.local';

/**
 * Whether this build has to ask for the `local-network` permission before it may mount.
 *
 * The shipping App is served from a public origin, so the browser gates its device request
 * on the permission and `ui/LocalNetworkGate.tsx` is what asks. The development origin is a
 * LAN address, and Local Network Access only gates public→local requests (LNA §2.2), so a
 * page served from the LAN is never prompted and could never answer the gate: the development
 * server runs in `development` mode and mounts without it (ADR 0082).
 *
 * `import.meta.env.MODE` is the signal because Vite replaces it in the dev transform as well
 * as in the build, which a custom `define` does not (that is why `WEIGHTS_MODE` keeps a
 * fallback). In Vitest the mode is `test`, so the suite exercises the shipping shape.
 */
export const DEVICE_NEEDS_PERMISSION = import.meta.env.MODE !== 'development';

/** The App's window name. `window.open` reuses a named window. */
export const APP_WINDOW = 'k7-app';

export function cacheKey(hash: string, modelSet: string): string {
  return `${hash}:${ANALYZER_VERSION}:${modelSet}`;
}
