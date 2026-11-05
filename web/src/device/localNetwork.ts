/**
 * The Local Network Access permission — the one thing between the hosted App and the player.
 *
 * `local.ts` fetches `http://k7.local/api/…` from this HTTPS origin. LNA §2.5 exempts a
 * `.local` host from the mixed-content block, but only when the `local-network` permission
 * has been granted: before a person answers the prompt, and after they deny it, the fetch is
 * refused and there is nothing the App can do about it. So the permission is asked for and
 * read here, in one place, and `ui/LocalNetworkGate.tsx` blocks the page until it is
 * granted.
 *
 * Chrome is the only browser with the API. Everything else answers `unsupported`, which is
 * the product's Chrome-only decision — a fact to state, not a fallback to hide.
 */

import { DEVICE_ORIGIN } from '../config';

export type LocalNetworkState = 'granted' | 'denied' | 'prompt' | 'unsupported';

/**
 * The permission's descriptor. TypeScript's `PermissionName` union predates Local Network
 * Access, so the name has to be cast; Chrome's Permissions API does accept it.
 */
const LOCAL_NETWORK = { name: 'local-network' } as unknown as PermissionDescriptor;

/** `RequestInit` does not declare the LNA option; the dictionary ignores what it does not know. */
interface LocalRequestInit extends RequestInit {
  targetAddressSpace?: 'local';
}

function permissions(): Permissions | null {
  if (typeof navigator === 'undefined') return null;
  const api = navigator.permissions;
  return typeof api?.query === 'function' ? api : null;
}

/**
 * The permission's state right now.
 *
 * A missing `navigator.permissions` is `unsupported`; so is a query that throws, which is
 * how Chrome answers a name it does not know (older versions, and every other browser).
 */
export async function queryLocalNetwork(): Promise<LocalNetworkState> {
  const api = permissions();
  if (api === null) return 'unsupported';
  try {
    const status = await api.query(LOCAL_NETWORK);
    switch (status.state) {
      case 'granted':
      case 'denied':
      case 'prompt':
        return status.state;
      default:
        return 'unsupported';
    }
  } catch {
    return 'unsupported';
  }
}

/**
 * Ask once, then read the answer.
 *
 * The request is the prompt: Chrome exposes no `request()` for this permission, so a single
 * `fetch` to the device is what makes the browser ask a person. It is expected to fail while
 * access is not granted (`Failed to fetch`), so the failure is swallowed and the permission
 * query afterwards is the fact. `true` means the request went out with permission; `false`
 * means it did not — denied, or a browser that does not implement the permission at all.
 */
export async function requestLocalNetwork(): Promise<boolean> {
  try {
    const init: LocalRequestInit = { mode: 'cors', targetAddressSpace: 'local' };
    await fetch(`${DEVICE_ORIGIN}/api/status`, init);
  } catch {
    /* refused, or the device is off: the permission query below decides which */
  }
  return (await queryLocalNetwork()) === 'granted';
}

/**
 * Subscribe to `PermissionStatus.onchange`, so the gate re-renders when a person grants or
 * revokes the permission in Chrome's site settings while the page is open. The returned
 * function unsubscribes and is safe to call more than once.
 */
export function watchLocalNetwork(callback: (state: LocalNetworkState) => void): () => void {
  const api = permissions();
  if (api === null) return () => {};

  let stopped = false;
  let status: PermissionStatus | null = null;

  void api
    .query(LOCAL_NETWORK)
    .then((result) => {
      if (stopped) return;
      status = result;
      result.onchange = () => {
        void queryLocalNetwork().then((next) => {
          if (!stopped) callback(next);
        });
      };
    })
    .catch(() => {
      /* unsupported: nothing to watch, and the gate already knows */
    });

  return () => {
    stopped = true;
    if (status !== null) status.onchange = null;
  };
}
