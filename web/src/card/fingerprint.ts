/**
 * The card fingerprint: a hash of what a card holds, used as a **Scope** only when no mark
 * is usable.
 *
 * G1 of `docs/specs/card-mark.md` §5: SHA-256 over the sorted lines `path:size:mtime` of
 * every track below `music/` - the walk the scan performs anyway, so it costs no extra I/O.
 * It is a change detector, not a name: adding one track changes it, and the covers scoped
 * to the old value are lost. That is the accepted price of a card that cannot keep a mark.
 *
 * The sort is by the path's **UTF-8 bytes**, never `localeCompare`: two machines with two
 * locales must agree on the same card. The hash comes from `src/lib/sha256.ts`, which falls
 * back to pure JavaScript when the Device copy withholds `crypto.subtle`.
 */

import { sha256Hex } from '../lib/sha256';

/** One track, as the walk that feeds the scan already describes it. */
export interface CardEntry {
  /** Snapshot path: below `music/`, `/`-separated, no leading slash. */
  path: string;
  /** Bytes. */
  size: number;
  /** Milliseconds since the epoch, as `File.lastModified` gives it. */
  mtime: number;
}

const encoder = new TextEncoder();

/** Lexicographic comparison of two byte arrays; the ordering the spec pins. */
function compareBytes(left: Uint8Array, right: Uint8Array): number {
  const length = Math.min(left.length, right.length);
  for (let at = 0; at < length; at++) {
    if (left[at] !== right[at]) return left[at] - right[at];
  }
  return left.length - right.length;
}

/**
 * The fingerprint of a walk. Every entry becomes the line `path:size:mtime`; the lines are
 * sorted by path bytes (the colon separator is part of the line, so the sort is done on the
 * path first and the remaining fields break ties) and joined with `\n`.
 *
 * The caller passes every track below `music/`. Nothing is filtered here: the function is
 * about the bytes it is given, and guessing which of them count would make two callers
 * disagree about the same card.
 */
export async function cardFingerprint(entries: CardEntry[]): Promise<string> {
  const keyed = entries.map((entry) => ({ entry, path: encoder.encode(entry.path) }));

  keyed.sort((left, right) => {
    const byPath = compareBytes(left.path, right.path);
    if (byPath !== 0) return byPath;
    // Duplicate paths should not happen, but if one does, the walk's order must not decide
    // the digest: size then mtime make the ordering total and identical on both machines.
    if (left.entry.size !== right.entry.size) return left.entry.size - right.entry.size;
    return left.entry.mtime - right.entry.mtime;
  });

  const payload = keyed
    .map(({ entry }) => `${entry.path}:${entry.size}:${entry.mtime}`)
    .join('\n');

  return sha256Hex(encoder.encode(payload));
}
