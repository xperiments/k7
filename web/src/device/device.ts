/**
 * Which Device this window has, if any — the one place the choice is made.
 *
 * One fact, no mode enum (k7-app.md §3): the App has a player when Chrome's Local Network
 * Access permission lets it reach `http://k7.local`, and otherwise it has **no** player. The
 * device serves no copy of this App, so there is no same-origin transport to choose between
 * and nothing to fall back to. Nothing opens a window and nothing waits for a second tab;
 * the gate in `ui/LocalNetworkGate.tsx` is what makes the one path possible.
 *
 * The choice is made once and kept; only `tryLocalConnect()` / `connectToDevice()` replace
 * a device, and only when there was none to begin with.
 */

import { useSyncExternalStore } from 'react';

import { DEVICE_ORIGIN } from '../config';
import { createLocalDevice, type LocalDevice } from './local';
import { requestLocalNetwork } from './localNetwork';
import { DeviceError, NO_LINK, type Device, type Link } from './transport';

/** Every device this module can hand out, discriminated by the kind of link it answers. */
interface NoneDevice extends Device {
  readonly kind: 'none';
}

type NamedDevice = LocalDevice | NoneDevice;

/** A device that has nothing to talk to. `link()` is `none` and every request says so. */
const noneDevice: NoneDevice = {
  kind: 'none',
  link: () => 'none',
  request: () => Promise.reject(NO_LINK),
  onLinkChange() {
    return () => {};
  },
};

let current: NamedDevice | null = null;
let unwatch: (() => void) | null = null;
const linkListeners = new Set<() => void>();

function notify(): void {
  for (const listener of [...linkListeners]) listener();
}

function adopt(device: NamedDevice): void {
  unwatch?.();
  current = device;
  unwatch = device.onLinkChange(notify);
}

function ensure(): NamedDevice {
  if (current !== null) return current;
  adopt(noneDevice);
  return noneDevice;
}

/** The Device this window has. Read, not cached by the caller: `link()` can change. */
export function getDevice(): Device {
  return ensure();
}

function getLink(): Link {
  return ensure().link();
}

function getServerLink(): Link {
  return 'none';
}

function subscribeLink(listener: () => void): () => void {
  ensure();
  linkListeners.add(listener);
  return () => {
    linkListeners.delete(listener);
  };
}

/** `link()` changes, for code that is not a render: the status poll asks again on one. */
export function subscribeDeviceLink(listener: () => void): () => void {
  return subscribeLink(listener);
}

/**
 * `link()` as a React value. `useSyncExternalStore` is the point: every view that renders a
 * link-dependent sentence re-renders when the player arrives or goes, without polling.
 */
export function useDeviceLink(): Link {
  return useSyncExternalStore(subscribeLink, getLink, getServerLink);
}

/** True while a probe is in flight, so the UI can say "looking" instead of "no player". */
let probing = false;
export function isProbing(): boolean {
  return probing;
}

/**
 * Try the local path and nothing else: no window, no opener, no fallback.
 *
 * This is what runs on load, after the gate holds the permission. It reaches the device when
 * the browser's Local Network Access permission has been granted and returns `false`
 * quietly otherwise, which the gate has already ruled out but a revoked permission can
 * still produce.
 */
export async function tryLocalConnect(): Promise<boolean> {
  if (ensure().link() !== 'none') return true;
  probing = true;
  notify();
  try {
    const local = createLocalDevice();
    await local.request('status');
    adopt(local);
    notify();
    return true;
  } catch {
    return false;
  } finally {
    probing = false;
    notify();
  }
}

/**
 * Connect a player from a click: make sure Local Network Access is granted, then reach the
 * device directly.
 *
 * There is no window to open and no second path: a refusal is thrown so the caller can put
 * the reason in front of the user.
 */
export async function connectToDevice(): Promise<void> {
  if (!(await requestLocalNetwork())) {
    throw new DeviceError(
      'Local network access was not granted. Allow it for this site in Chrome, then press Connect again.',
      'blocked',
    );
  }
  if (await tryLocalConnect()) return;
  throw new DeviceError(
    `The player did not answer at ${DEVICE_ORIGIN}. Check that it is on and on this network.`,
    'unreachable',
  );
}
