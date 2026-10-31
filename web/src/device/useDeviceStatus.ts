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
 * Polls `/api/status` every five seconds. On failure the last known status is kept and
 * the device is marked unreachable, so a Player view that was showing can say the device
 * is off rather than silently becoming the Prepare view.
 */
export function useDeviceStatus(intervalMs = 5000): DeviceSnapshot {
  const [snapshot, setSnapshot] = useState<DeviceSnapshot>(INITIAL);

  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | undefined;

    const poll = async () => {
      const status = await fetchStatus();
      if (cancelled) return;
      setSnapshot((previous) =>
        status ? { status, reachable: true } : { status: previous.status, reachable: false },
      );
      timer = setTimeout(() => void poll(), intervalMs);
    };

    void poll();
    return () => {
      cancelled = true;
      if (timer !== undefined) clearTimeout(timer);
    };
  }, [intervalMs]);

  return snapshot;
}
