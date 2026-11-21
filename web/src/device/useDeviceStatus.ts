import { useEffect, useState } from 'react';

import { getDevice, subscribeDeviceLink, tryLocalConnect } from './device';
import { forgetDeviceOrigin } from './origin';
import type { DeviceStatus } from './status';

export interface DeviceSnapshot {
  /** The last successful answer; null when the device has never answered. */
  status: DeviceStatus | null;
  reachable: boolean;
}

const INITIAL: DeviceSnapshot = { status: null, reachable: false };

/**
 * One poll, tolerant of every failure an absent device can produce.
 *
 * The request goes through the `Device` seam: local, the answer arrives from the device
 * over the network; with no link at all it rejects at once and the absence is `null`, which
 * every view already has copy for. Retrying the link belongs to the poll below, not to this
 * read: a caller resolving a library source must not wait on a probe.
 */
export async function fetchStatus(): Promise<DeviceStatus | null> {
  try {
    return await getDevice().request('status');
  } catch {
    return null;
  }
}

/**
 * One poller for the whole page. The sidebar, the Player area, Scan and Media all ask
 * the same question, and four pollers asking it four times a second is not a design.
 * The first subscriber starts the loop; the last one stops it.
 */
let snapshot: DeviceSnapshot = INITIAL;
let timer: ReturnType<typeof setInterval> | null = null;
let stopWatchingLink: (() => void) | null = null;
let inFlight = false;
/** Polls in a row that failed. One is a beat; a run is an absent device. */
let misses = 0;
const listeners = new Set<() => void>();

/**
 * How many polls in a row may fail before the page says the device is absent. A single
 * missed beat is a busy player, a request lost on the way, or a cold `k7.local` lookup -
 * none of them a device that is off, and each of them used to flip the page, its badge
 * and the Player for five seconds.
 */
const ABSENT_AFTER = 3;

function publish(next: DeviceSnapshot): void {
  snapshot = next;
  for (const listener of listeners) listener();
}

async function pollOnce(): Promise<void> {
  if (inFlight) return;
  inFlight = true;
  try {
    /* The link is an observed fact, so a probe that lost the first load's cold `k7.local`
     * lookup is tried again here. Asking the status poll to recover is what keeps the page
     * from waiting on a `none` device until someone reloads it. */
    if (getDevice().link() === 'none') await tryLocalConnect();
    const status = await fetchStatus();
    if (status) {
      misses = 0;
      /* The reply named the device's address (`X-K7-Net`) on its way in, so the next
       * request skips the mDNS lookup without asking anything extra. */
      publish({ status, reachable: true });
      return;
    }
    misses += 1;
    /* An address is a lease: a miss may be a new one. The next poll goes back to the name,
     * which is the thing that does not change. */
    forgetDeviceOrigin();
    publish({
      status: snapshot.status,
      reachable: misses < ABSENT_AFTER ? snapshot.reachable : false,
    });
  } finally {
    inFlight = false;
  }
}

function start(intervalMs: number): void {
  if (timer !== null) return;
  /* A link that arrives or dies is a reason to ask now rather than at the next tick: the
   * poll is the protocol's heartbeat, and Connect should show its answer immediately. */
  stopWatchingLink ??= subscribeDeviceLink(() => void pollOnce());
  void pollOnce();
  timer = setInterval(() => void pollOnce(), intervalMs);
}

function stop(): void {
  if (timer === null) return;
  clearInterval(timer);
  timer = null;
  stopWatchingLink?.();
  stopWatchingLink = null;
}

/** The last answer without subscribing: for a click handler that needs the card state. */
export function deviceSnapshot(): DeviceSnapshot {
  return snapshot;
}

/** Refresh now, e.g. after a rescan was accepted or a card was inserted. */
export function refreshDevice(): Promise<void> {
  return pollOnce();
}

/**
 * The device's Card state and what it is playing. On failure the last known status is
 * kept and the device is marked unreachable, so an area that was showing can say the
 * device is off rather than silently emptying itself.
 */
export function useDeviceStatus(intervalMs = 5000): DeviceSnapshot {
  const [value, setValue] = useState<DeviceSnapshot>(snapshot);

  useEffect(() => {
    const listener = () => setValue(snapshot);
    listeners.add(listener);
    start(intervalMs);
    listener();
    return () => {
      listeners.delete(listener);
      if (listeners.size === 0) stop();
    };
  }, [intervalMs]);

  return value;
}
