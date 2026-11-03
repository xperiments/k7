/**
 * The device, directly — `fetch('http://k7.local/api/…')` from the hosted App.
 *
 * This is the simplest shape the product could have: no bridge window, no opener, no
 * second tab. The Local Network Access specification allows exactly it, and the current
 * text is explicit about the part everyone assumes is impossible:
 *
 *   "Many local network servers do not run HTTPS… One solution is to bypass mixed content
 *    checks in situations where the request is known to be a local network request. This is
 *    known in a few situations: when the hostname is an IP literal identified as local… or
 *    when the hostname is a `.local` domain."            — LNA §2.5
 *
 * `k7.local` is that second case, and the request is then gated on the `local-network`
 * permission rather than on the scheme.
 *
 * **Chrome 156 does not do this yet.** Measured on 2026-11-03 from the deployed origin with
 * the permission granted: a fetch to `http://k7.local` and to `http://192.168.0.38` both
 * fail with `TypeError: Failed to fetch`, and not one request — not even the CORS preflight —
 * reaches the network. The permission exists; the mixed-content bypass is unshipped.
 *
 * So this transport is dormant by construction: `device.ts` tries it first, and a failed
 * probe falls through to the bridge, which is the path that works today. When a browser
 * ships §2.5, the same code starts answering and the Launcher stops being required — with
 * the device's CORS headers (app_services.c) already in place to receive it.
 */

import { DEVICE_ORIGIN } from '../config';
import { createDirectDevice } from './direct';
import type { Device } from './transport';

export interface LocalDevice extends Device {
  readonly kind: 'local';
  /** What this transport is waiting for, for the sentence the UI shows. */
  readonly blocked: 'mixed content' | null;
}

export interface LocalOptions {
  /** Injected in tests: the page's own `fetch`. */
  fetch?: typeof fetch;
  /** Injected in tests: `null` means this window carries no audio. */
  audio?: null;
}

/** `RequestInit` does not declare the LNA option; the dictionary ignores what it does not know. */
interface LocalRequestInit extends RequestInit {
  targetAddressSpace?: 'local';
}

export function createLocalDevice(options: LocalOptions = {}): LocalDevice {
  const pageFetch = options.fetch ?? ((input: RequestInfo | URL, init?: RequestInit) => fetch(input, init));
  const localFetch = (input: string, init?: RequestInit): Promise<Response> => {
    // The direct device asks with paths; this one has to name the host, and say that it
    // knows the host is on the local network — which is what asks the browser for the
    // permission instead of blocking the request as mixed content.
    const absolute = input.startsWith('http') ? input : `${DEVICE_ORIGIN}${input}`;
    const withSpace: LocalRequestInit = { ...init, mode: 'cors', targetAddressSpace: 'local' };
    return pageFetch(absolute, withSpace);
  };

  const inner = createDirectDevice({
    fetch: localFetch,
    audio: options.audio,
    streamSrc: `${DEVICE_ORIGIN}/stream`,
  });

  return {
    ...inner,
    kind: 'local',
    // `direct` is the Device copy; this one is a different fact and says so.
    link: () => 'local' as const,
    blocked: null,
  } as LocalDevice;
}
