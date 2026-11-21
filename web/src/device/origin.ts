/**
 * Where the device answers.
 *
 * `k7.local` is a name the browser resolves over mDNS, and every request to it pays that
 * lookup again: on a page that asks for the status on a timer, the heartbeat spends its
 * time resolving rather than asking. The device can name its own address -
 * `GET /api/net` answers `{ ip, port }` - so the App asks once over the name and sends every
 * later request to `http://<ip>:<port>`.
 *
 * The address is a lease, not a fact: the router may hand the device a different one. A
 * request that fails to reach it forgets it, and the next one goes back to the name, which
 * is the thing that does not change.
 */
import { DEVICE_ORIGIN } from '../config';

/** The option Local Network Access reads; `RequestInit` does not declare it. */
interface LocalRequestInit extends RequestInit {
  targetAddressSpace?: 'local';
}

let resolved: string | null = null;

/** The base every device request is sent to: the name until the address is known. */
export function deviceOrigin(): string {
  return resolved ?? DEVICE_ORIGIN;
}

/** True while the base is still the mDNS name. */
export function deviceOriginIsName(): boolean {
  return resolved === null;
}

/**
 * Ask the device for its address over the name, and keep it. Returns the base it settled on:
 * the address, or the name when the device did not answer or named something that is not a
 * port and an address.
 */
export async function resolveDeviceOrigin(
  fetchImpl: typeof fetch = fetch,
): Promise<string> {
  try {
    const init: LocalRequestInit = { mode: 'cors', targetAddressSpace: 'local' };
    const response = await fetchImpl(`${DEVICE_ORIGIN}/api/net`, init);
    if (!response.ok) {
      resolved = null;
      return deviceOrigin();
    }
    const body = (await response.json()) as { ip?: unknown; port?: unknown };
    const ip = typeof body.ip === 'string' ? body.ip.trim() : '';
    const port = typeof body.port === 'number' ? body.port : Number(body.port);
    if (!ip || !Number.isInteger(port) || port <= 0 || port > 65535) {
      resolved = null;
      return deviceOrigin();
    }
    resolved = `http://${ip}:${port}`;
  } catch {
    /* No address yet, or the request was refused: the name is still the way in. */
    resolved = null;
  }
  return deviceOrigin();
}

/** Forget the address: the next request goes back to the name and resolves again. */
export function forgetDeviceOrigin(): void {
  resolved = null;
}
