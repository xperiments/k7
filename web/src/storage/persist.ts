/**
 * Storage persistence, requested when the value has been demonstrated (O5).
 *
 * The analysis store and the weights are only as durable as the browser lets them be:
 * a site without persistence is evicted first under pressure. Asking is not granting, so
 * every answer is reported rather than assumed. `navigator` is injectable for tests and
 * for an insecure origin, where the same API is present but the origin is not secure.
 */

export type Persistence = 'granted' | 'denied' | 'unsupported';

export interface StorageManagerLike {
  persisted?: () => Promise<boolean>;
  persist?: () => Promise<boolean>;
  estimate?: () => Promise<{ usage?: number; quota?: number }>;
}

export interface StorageNavigatorLike {
  readonly storage?: StorageManagerLike;
}

/**
 * Asks the browser to keep this origin's data. An origin that is already persisted is
 * reported as granted without a second prompt.
 */
export async function requestPersistence(
  nav: StorageNavigatorLike = navigator,
): Promise<Persistence> {
  const storage = nav.storage;
  if (!storage || typeof storage.persist !== 'function') return 'unsupported';

  if (typeof storage.persisted === 'function') {
    try {
      if (await storage.persisted()) return 'granted';
    } catch {
      // A browser that cannot answer may still grant; ask anyway.
    }
  }

  try {
    return (await storage.persist()) ? 'granted' : 'denied';
  } catch {
    // A denied request is a refusal to keep data, not a broken app.
    return 'denied';
  }
}

/**
 * How much the origin uses and how much it may use, in bytes. Null when the browser
 * cannot say, so Settings can omit the sentence instead of printing `undefined`.
 */
export async function storageEstimate(
  nav: StorageNavigatorLike = navigator,
): Promise<{ usage: number; quota: number } | null> {
  const storage = nav.storage;
  if (!storage || typeof storage.estimate !== 'function') return null;

  try {
    const estimate = await storage.estimate();
    const usage = estimate?.usage;
    const quota = estimate?.quota;
    return typeof usage === 'number' && typeof quota === 'number'
      ? { usage, quota }
      : null;
  } catch {
    return null;
  }
}
