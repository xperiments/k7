/** SHA-256 as lowercase hex. The one copy, so a URL and its cache key agree. */
export async function sha256Hex(data: BufferSource): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', data);
  return Array.from(new Uint8Array(digest))
    .map((byte) => byte.toString(16).padStart(2, '0'))
    .join('');
}

/**
 * Verification is not a size check: a weight is accepted only when its bytes hash to the
 * pinned value. The comparison is case-insensitive because manifests are written by hand.
 */
export async function verifySha256(data: BufferSource, expected: string): Promise<boolean> {
  return (await sha256Hex(data)) === expected.trim().toLowerCase();
}
