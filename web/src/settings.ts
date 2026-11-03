/**
 * What this browser remembers for itself: the Provider that asks for a Playlist's
 * filters, and the Invidious Instance the video search talks to.
 *
 * `localStorage`, not the device: the key is the owner's, it must never be written to
 * the device's flash, and the page is served from the device's origin so site data is
 * per-browser (ADR 0053). Lost with site data, which is acceptable for both.
 */

export interface Settings {
  /** OpenAI-compatible endpoint, e.g. https://api.openai.com/v1 */
  providerBaseUrl: string;
  /** The model that answers with an Intent, never with tracks. */
  providerModel: string;
  /** Never rendered after it is saved, never sent to the device. */
  providerKey: string;
  /** The Invidious server the video search talks to. */
  invidious: string;
}

const STORAGE_KEY = 'k7.settings.v1';

export const DEFAULT_SETTINGS: Settings = {
  providerBaseUrl: '',
  providerModel: '',
  providerKey: '',
  invidious: 'https://inv.nadeko.net',
};

function read(): Settings {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return { ...DEFAULT_SETTINGS };
    const parsed = JSON.parse(raw) as Partial<Settings>;
    return {
      providerBaseUrl: typeof parsed.providerBaseUrl === 'string' ? parsed.providerBaseUrl : '',
      providerModel: typeof parsed.providerModel === 'string' ? parsed.providerModel : '',
      providerKey: typeof parsed.providerKey === 'string' ? parsed.providerKey : '',
      invidious: typeof parsed.invidious === 'string' ? parsed.invidious : DEFAULT_SETTINGS.invidious,
    };
  } catch {
    return { ...DEFAULT_SETTINGS };
  }
}

let cache: Settings = read();
const listeners = new Set<() => void>();

export function getSettings(): Settings {
  return cache;
}

export function hasProvider(settings: Settings = cache): boolean {
  return Boolean(settings.providerBaseUrl && settings.providerModel && settings.providerKey);
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
