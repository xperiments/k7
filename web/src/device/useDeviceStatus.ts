import { useEffect, useState } from 'react';
import { parseStatus, type DeviceStatus } from './status';

export interface DeviceSnapshot {
  /** The last successful answer; null when the device has never answered. */
  status: DeviceStatus | null;
  reachable: boolean;
}

const INITIAL: DeviceSnapshot = { status: null, reachable: false };

/** One poll, tolerant of every failure an absent device can produce. */
export async function fetchStatus(timeoutMs = 4000): Promise<DeviceStatus | null> {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch('/api/status', {
      cache: 'no-store',
      headers: { accept: 'application/json' },
      signal: controller.signal,
    });
    if (!response.ok) return null;
    return parseStatus(await response.json());
  } catch {
    return null;
  } finally {
    clearTimeout(timeout);
  }
}

/**
 * One poller for the whole page. The sidebar, the Player area, Scan and Media all ask
 * the same question, and four pollers asking it four times a second is not a design.
 * The first subscriber starts the loop; the last one stops it.
 */
let snapshot: DeviceSnapshot = INITIAL;
let timer: ReturnType<typeof setInterval> | null = null;
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
  void pollOnce();
  timer = setInterval(() => void pollOnce(), intervalMs);
}

function stop(): void {
  if (timer === null) return;
  clearInterval(timer);
  timer = null;
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
