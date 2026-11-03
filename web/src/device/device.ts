/**
 * Which Device this window has, if any — the one place the choice is made.
 *
 * Three facts, no mode enum (k7-app.md §3): the App is the **Device copy** when the device
 * itself served it, it has a **bridge** when a Launcher opened it (or when it opened one),
 * and otherwise it has **no** player. The views ask `getDevice()` and never ask which.
 *
 * The choice is made once and kept, because a bridge that loses its Launcher and finds it
 * again is still the same bridge (`link()` says so); only `connectToDevice()` replaces a
 * device, and only when there was none to begin with.
 */

import { useSyncExternalStore } from 'react';

import { DEVICE_ORIGIN } from '../config';
import { createBridgeDevice, type BridgeDevice } from './bridge';
import { createDirectDevice, type DirectDevice } from './direct';
import { NO_LINK, type Device, type Link } from './transport';

/** Every device this module can hand out, discriminated by the kind of link it answers. */
interface NoneDevice extends Device {
  readonly kind: 'none';
}

type NamedDevice = DirectDevice | BridgeDevice | NoneDevice;

/** The Device copy: the App's own bytes, served by the device, at its own origin. */
function isDeviceCopy(): boolean {
  return typeof location !== 'undefined' && location.origin === DEVICE_ORIGIN;
}

/**
 * Presence of an opener is the only fact this side can read: a cross-origin
 * `window.opener.location` throws, and trust is decided per message anyway (S2). A
 * `window.opener` that is not the Launcher simply never sends anything acceptable.
 */
function hasOpener(): boolean {
  if (typeof window === 'undefined') return false;
  return window.opener !== null && window.opener !== undefined;
}

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

function select(): NamedDevice {
  if (isDeviceCopy()) return createDirectDevice();
  if (hasOpener()) return createBridgeDevice({ window });
  return noneDevice;
}

function ensure(): NamedDevice {
  if (current !== null) return current;
  const device = select();
  adopt(device);
  return device;
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
 * link-dependent sentence re-renders when the bridge arrives or goes, without polling.
 */
export function useDeviceLink(): Link {
  return useSyncExternalStore(subscribeLink, getLink, getServerLink);
}

/**
 * Connect a player from a click: opens the Launcher in the `k7-bridge` window (W1, W4).
 *
 * Only ever called from a user gesture, because `window.open` without one is blocked and a
 * blocked popup is `null`. A window that is already the Device copy has nothing to connect.
 */
export async function connectToDevice(): Promise<void> {
  if (isDeviceCopy()) return;
  const device = ensure();
  if (device.kind === 'bridge') {
    await device.connect();
    return;
  }
  const bridge: BridgeDevice = createBridgeDevice({ window });
  adopt(bridge);
  notify();
  await bridge.connect();
}
