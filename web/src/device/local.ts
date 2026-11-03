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
 * **Measured, 2026-11-03, Chrome 156, on the deployed origin:** a person granted the
 * `local-network` prompt and `fetch('http://k7.local/api/status')` answered **200** with the
 * device's CORS headers. It works. Two earlier attempts of mine read as a refusal for a
 * reason worth recording: a permission granted through Chrome's debugging protocol is not a
 * permission granted by a person, and `Browser.grantPermissions` produced `Failed to fetch`
 * in 1 ms with nothing on the wire. The prompt has to be answered.
 *
 * `device.ts` tries this first. A browser that will not do it — Safari, Firefox, a person
 * who denies the prompt — falls through to the bridge, which is why the Launcher still
 * exists. The device's CORS headers (app_services.c) are what let the request through once
 * the connection is permitted.
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
