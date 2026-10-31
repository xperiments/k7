/**
 * A weight is the bytes at one URL with one known hash. Either changing is a cache miss,
 * so a model swapped at the same URL cannot produce a stale hit.
 */
export function weightCacheKey(url: string, sha256: string): string {
  return `${sha256.trim().toLowerCase()}\n${url}`;
}
