import { sha256Hex } from '../lib/sha256';

/** SHA-256 as lowercase hex. The one copy, so a URL and its cache key agree. */
export { sha256Hex };

/**
 * Verification is not a size check: a weight is accepted only when its bytes hash to the
 * pinned value. The comparison is case-insensitive because manifests are written by hand.
 */
export async function verifySha256(data: BufferSource, expected: string): Promise<boolean> {
  return (await sha256Hex(data as ArrayBuffer | Uint8Array)) === expected.trim().toLowerCase();
}
