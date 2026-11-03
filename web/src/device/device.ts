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

import { createBridgeDevice, type BridgeDevice } from './bridge';
import { createDirectDevice, type DirectDevice } from './direct';
import { createLocalDevice, type LocalDevice } from './local';
import { DeviceError, NO_LINK, type Device, type Link } from './transport';

/** Every device this module can hand out, discriminated by the kind of link it answers. */
interface NoneDevice extends Device {
  readonly kind: 'none';
}

type NamedDevice = DirectDevice | BridgeDevice | LocalDevice | NoneDevice;

/**
 * The Device copy: the App's own bytes, served by the device. The device is the only thing
 * that serves this App over plain HTTP — the Hosted App is always HTTPS — so the scheme is
 * the fact, not the hostname. Reached by IP it is still the Device copy, and it must keep
 * working: that page is what you open when mDNS does not.
 */
function isDeviceCopy(): boolean {
  return typeof location !== 'undefined' && location.protocol === 'http:';
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
/** True while a probe is in flight, so the UI can say "looking" instead of "no player". */
let probing = false;
export function isProbing(): boolean {
  return probing;
}

/**
 * Try the direct path and nothing else: no window, no opener, no fallback.
 *
 * This is what runs on load. It reaches the device if the browser's Local Network Access
 * permission has been granted — the first attempt is what raises the prompt — and returns
 * false quietly otherwise, which is the standalone case and not an error.
 */
export async function tryLocalConnect(): Promise<boolean> {
  if (isDeviceCopy()) return false;
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

export async function connectToDevice(): Promise<void> {
  if (isDeviceCopy()) return;
  // The direct path first, and if it answers there is nothing to open.
  if (await tryLocalConnect()) return;
  if (ensure().link() !== 'none') return;
  const device = ensure();
  if (device.kind === 'bridge') {
    await device.connect();
    return;
  }
  // The bridge, in the window named `k7-bridge`: for a browser that will not do the direct
  // path, and for a person who denied the prompt.
  const bridge: BridgeDevice = createBridgeDevice({ window });
  adopt(bridge);
  notify();
  await bridge.connect();
  // The window opening is not the bridge answering. A popup that lands on an error page —
  // `k7.local` not resolving yet, say — otherwise leaves the tab silent and the user with a
  // black window and no sentence, so the wait has a deadline and a cause.
  await waitForWelcome(bridge, 5_000);
}

/** Resolves when the Launcher has said `welcome`; rejects with what to look at instead. */
function waitForWelcome(device: Device, ms: number): Promise<void> {
  if (device.link() === 'bridge') return Promise.resolve();
  return new Promise((resolve, reject) => {
    let stop: (() => void) | null = null;
    const timer = setTimeout(() => {
      stop?.();
      reject(
        new DeviceError(
          'The bridge window opened but did not answer. Check that http://k7.local/ ' +
            'loads there, then press Connect again.',
          'unreachable',
        ),
      );
    }, ms);
    stop = device.onLinkChange(() => {
      if (device.link() !== 'bridge') return;
      clearTimeout(timer);
      stop?.();
      resolve();
    });
  });
}
