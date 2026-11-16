import { useEffect, useState } from 'react';

import { getDevice, subscribeDeviceLink, tryLocalConnect } from './device';
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
 * over the network; with no link at all it asks for one first. The link is an observed fact,
 * so a probe that failed early — the first load's `k7.local` lookup is the cold one — is
 * tried again on the next beat instead of leaving the page with a `none` device until a
 * reload. A probe and the status request are two round trips only in the beat that links.
 */
export async function fetchStatus(): Promise<DeviceStatus | null> {
  try {
    if (!(await tryLocalConnect())) return null;
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
const listeners = new Set<() => void>();

function publish(next: DeviceSnapshot): void {
  snapshot = next;
  for (const listener of listeners) listener();
}

async function pollOnce(): Promise<void> {
  if (inFlight) return;
  inFlight = true;
  try {
    const status = await fetchStatus();
    publish(status ? { status, reachable: true } : { status: snapshot.status, reachable: false });
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
