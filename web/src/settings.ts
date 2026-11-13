/**
 * What this browser remembers for itself: the Invidious **Instance** the video search talks to,
 * and the size of the analysis worker pool.
 *
 * `localStorage`, not the device: the App writes nothing to the device's flash, and the page is
 * served from the App's own origin, so site data is per-browser. Lost with site data, which is
 * acceptable for both.
 */

/** The pool never asks for more than this, however it is configured or detected. */
export const MAX_ANALYSIS_WORKERS = 8;

/** Auto means "detect at boot"; a number is a pinned request for that many workers. */
export type AnalysisWorkers = 'auto' | number;

export interface Settings {
  /** The Invidious server the video search talks to. */
  invidious: string;
  /** Analysis workers: auto-detected at boot, or pinned. Applies from the next reload. */
  workers: AnalysisWorkers;
}

const STORAGE_KEY = 'k7.settings.v2';

/**
 * Before the built-in model, this store held a Provider's base URL, model and API key. The key
 * is not migrated: there is no assistant setting to move it to, and leaving a secret in a
 * superseded blob is the one thing a clean break must not do. The key goes with the blob.
 */
const LEGACY_KEY = 'k7.settings.v1';

export const DEFAULT_SETTINGS: Settings = {
  invidious: 'https://inv.nadeko.net',
  workers: 'auto',
};

function workers(raw: unknown): AnalysisWorkers {
  if (raw === 'auto') return 'auto';
  if (
    typeof raw === 'number' &&
    Number.isInteger(raw) &&
    raw >= 1 &&
    raw <= MAX_ANALYSIS_WORKERS
  ) {
    return raw;
  }
  return DEFAULT_SETTINGS.workers;
}

function read(): Settings {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return { ...DEFAULT_SETTINGS };
    const parsed = JSON.parse(raw) as Partial<Settings>;
    return {
      invidious: typeof parsed.invidious === 'string' ? parsed.invidious : DEFAULT_SETTINGS.invidious,
      workers: workers(parsed.workers),
    };
  } catch {
    return { ...DEFAULT_SETTINGS };
  }
}

/* Runs once per page: v1 is gone, and the Provider key it carried is gone with it. */
try {
  localStorage.removeItem(LEGACY_KEY);
} catch {
  /* A browser that refuses storage keeps the settings for this page's life. */
}

let cache: Settings = read();
const listeners = new Set<() => void>();

export function getSettings(): Settings {
  return cache;
}

export function setSettings(patch: Partial<Settings>): void {
  cache = { ...cache, ...patch };
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(cache));
  } catch {
    /* A browser that refuses storage keeps the settings for this page's life. */
  }
  for (const listener of listeners) listener();
}

export function subscribeSettings(listener: () => void): () => void {
  listeners.add(listener);
  return () => listeners.delete(listener);
}
