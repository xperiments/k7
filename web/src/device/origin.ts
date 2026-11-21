/**
 * Where the device answers.
 *
 * `k7.local` is a name the browser resolves over mDNS, and every request to it pays that
 * lookup again: on a page that asks for the status on a timer, the heartbeat spends its time
 * resolving rather than asking. The device knows its own address and says it on **every
 * reply**, as `X-K7-Net: <ip>:<port>` (front-door.md S2), so the App learns it from a reply
 * it was making anyway - the page load or the first status - and never has to ask for it.
 *
 * The address is a lease, not a fact: the router may hand the device a different one. A
 * request that fails to reach it forgets it, and the next one goes back to the name, which
 * is the thing that does not change.
 */
import { DEVICE_ORIGIN } from '../config';

/** The reply header the device names its address on. */
export const ADDRESS_HEADER = 'x-k7-net';

let resolved: string | null = null;

/** The base every device request is sent to: the name until a reply has named the address. */
export function deviceOrigin(): string {
  return resolved ?? DEVICE_ORIGIN;
}

/** Take the address a reply named. A reply without one says nothing about the address. */
export function noteDeviceOrigin(raw: string | null | undefined): void {
  const value = raw?.trim();
  if (!value) return;
  const at = value.lastIndexOf(':');
  if (at <= 0) return;
  const ip = value.slice(0, at).trim();
  const port = Number(value.slice(at + 1));
  if (!ip || !Number.isInteger(port) || port <= 0 || port > 65535) return;
  resolved = `http://${ip}:${port}`;
}

/** Forget the address: the next request goes back to the name. */
export function forgetDeviceOrigin(): void {
  resolved = null;
}
