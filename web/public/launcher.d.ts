/**
 * Types for `public/launcher.js`, which is plain JavaScript because the device serves it
 * with no build step. The tests import the module through this file, so the injected
 * window, fetch, timers and audio are described precisely enough to type-check fakes.
 *
 * The op table is deliberately `Record<string, ...>`: the launcher could grow a name the
 * App does not know, and `tests/bridge-contract.test.ts` is what catches it, at runtime,
 * against the spec's own table.
 */

export interface LauncherEvent {
  readonly origin: string;
  /** The source `WindowProxy`; compared by identity against the session table. */
  readonly source: unknown;
  readonly data: unknown;
}

export interface LauncherWindow {
  addEventListener(type: 'message', handler: (event: LauncherEvent) => void): void;
  removeEventListener(type: 'message', handler: (event: LauncherEvent) => void): void;
  postMessage(message: unknown, targetOrigin: string): void;
}

export interface LauncherRequestInit {
  method?: string;
  headers?: Record<string, string>;
  body?: string | Uint8Array;
  cache?: string;
}

export interface LauncherResponse {
  readonly ok: boolean;
  readonly status: number;
  text(): Promise<string>;
}

export type LauncherFetch = (url: string, init?: LauncherRequestInit) => Promise<LauncherResponse>;

export interface LauncherTimers {
  setTimeout(handler: () => void, ms: number): unknown;
  clearTimeout(handle: unknown): void;
}

/** The Launcher's own `<audio>`, driven by `stream.*` (O3). */
export interface LauncherAudio {
  /** Kept current by the page's `play`/`pause` events. */
  playing?: boolean;
  /** Set when the browser refused `play()` under the autoplay policy. */
  blocked?: boolean;
  /** 0..1. The page forwards this to the element. */
  volume: number;
  /** Resolves when playback starts; a rejection or `false` is a refusal. */
  play(): void | boolean | Promise<void | boolean>;
  pause(): void | Promise<void>;
}

export interface LauncherSession {
  readonly origin: string;
  readonly version: number;
  readonly lastSeen: number;
  readonly source: unknown;
}

export interface LauncherBridgeOptions {
  win: LauncherWindow;
  fetch: LauncherFetch;
  /** The device's own origin, e.g. `http://k7.local`. Every request is against it. */
  origin: string;
  /** §S3: the baked-in App origin list. A `*` entry is ignored, never trusted. */
  appOrigins: readonly string[];
  now?: () => number;
  timers?: LauncherTimers;
  audio?: LauncherAudio;
}

export interface LauncherBridge {
  start(): void;
  stop(): void;
  sessions(): LauncherSession[];
  audio(): LauncherAudio;
}

export interface LauncherOpSpec {
  readonly timeout: number;
  readonly bytes: boolean;
}

export declare const BRIDGE_VERSION: number;
export declare const SESSION_IDLE_MS: number;
export declare const LIMITS: {
  readonly request: number;
  readonly json: number;
  readonly response: number;
};

/** `window.K7_APP_ORIGINS` when the build supplied one, else the one shipping origin. */
export declare const APP_ORIGINS: readonly string[];

export declare const OP_SPECS: Readonly<Record<string, LauncherOpSpec>>;

export declare function createLauncherBridge(options: LauncherBridgeOptions): LauncherBridge;
