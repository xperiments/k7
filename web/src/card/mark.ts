/**
 * `k7.card`: the name a card carries for itself.
 *
 * A card is a directory holding `music/`. The App writes this small file at its root so
 * that the browser's card-level state - covers, the Last image, a run in progress - belongs
 * to one card and not to whichever card happens to be inserted. It is a name, not a
 * credential: nothing on a FAT32 volume can be an identity, and the mark is never asked to
 * authorise anything.
 *
 * Nothing here touches the DOM or IndexedDB. The only environment it needs is randomness,
 * which is a parameter, so a test can pin an id without a browser.
 *
 * The policy is §4 of `docs/specs/card-mark.md`. The one rule that matters more than the
 * rest: a file with our name and contents we do not understand is **never** overwritten.
 * It is either a newer format's mark or somebody else's mistake, and replacing it destroys
 * the identity its cache hangs from.
 */

/** The mark's file name, at the card root beside `k7.db`. */
export const MARK_NAME = 'k7.card';

/** The mark's own format version, not the App's. */
export const MARK_VERSION = 1;

export interface CardMark {
  v: 1;
  /** A UUID v4, the card's name. Never a credential. */
  id: string;
  /** ISO 8601, UTC. Informational; the App never derives identity from it. */
  created: string;
  /** Optional, user-chosen, for the interface. */
  label?: string;
}

/**
 * What reading `k7.card` can produce:
 *
 * - a mark this build understands;
 * - `absent` - an empty or missing file, the one state the App may fill in;
 * - `newer` - a mark from a format this build does not know, with a usable id;
 * - `foreign` - anything else, including a file with our name and no usable id.
 *
 * Only the first can be adopted. `absent` may be created when the card is writable.
 * `newer` and `foreign` are read-only and lead to the fingerprint scope (§5), with
 * **re-mark this card** left as an explicit act.
 */
export type MarkState = CardMark | 'absent' | 'newer' | 'foreign';

/** Fills the given sixteen-byte buffer with random bytes; the shape `getRandomValues` has. */
export type RandomSource = (bytes: Uint8Array) => Uint8Array;

export interface DecideOptions {
  /** A read-only card (write-protected SD, a dropped folder, no picker) is never written. */
  writable: boolean;
  /** When the mark is created. Defaults to now; injected for tests. */
  now?: string;
  /** The label to give a newly created mark. */
  label?: string;
  /** Injected for tests; production callers pass nothing. */
  random?: RandomSource;
}

export interface MarkDecision {
  action: 'use' | 'create' | 'unmarked';
  /** Only present when `action` is `create`; nothing else may write a mark. */
  write?: CardMark;
}

/**
 * `crypto.randomUUID` is secure-context-only, and the Device copy at `http://k7.local/app`
 * is not a secure context. `getRandomValues` is not gated that way, gives the same 122
 * bits, and works wherever a card can be picked.
 */
function systemRandom(bytes: Uint8Array): Uint8Array {
  if (!globalThis.crypto?.getRandomValues) {
    throw new Error('This browser cannot generate a card mark: crypto.getRandomValues is missing.');
  }
  globalThis.crypto.getRandomValues(bytes);
  return bytes;
}

/**
 * A UUID in canonical form. This is deliberately shape rather than version: the App writes
 * v4, but a mark is a name, and rejecting a v1-looking id from a future writer would turn a
 * usable card into an unmarked one for no gain.
 */
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

function formatUuid(bytes: Uint8Array): string {
  const hex = Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
  return (
    `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-` +
    `${hex.slice(16, 20)}-${hex.slice(20)}`
  );
}

/**
 * A new card name: sixteen random bytes as a UUID v4. `now` is passed in rather than read
 * from the clock so the file the App writes is the same one the caller described.
 */
export function newCardMark(
  now: string,
  label?: string,
  random: RandomSource = systemRandom,
): CardMark {
  const bytes = new Uint8Array(16);
  random(bytes);
  // RFC 4122: version 4 in the high nibble of byte 6, variant 10 in the top bits of byte 8.
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;

  const mark: CardMark = { v: MARK_VERSION, id: formatUuid(bytes), created: now };
  if (label !== undefined) mark.label = label;
  return mark;
}

/**
 * Read the file's text.
 *
 * A zero-length file is `absent`; whitespace is not, because we never write a mark that is
 * only whitespace, and guessing would overwrite something we did not write. `v` decides the
 * branch before anything else: an integer above ours is `newer` (and never written), and an
 * integer we do not understand below ours is `foreign`.
 */
export function parseCardMark(text: string): MarkState {
  if (text.length === 0) return 'absent';

  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch {
    return 'foreign';
  }

  if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) return 'foreign';
  const record = parsed as Record<string, unknown>;

  const version = record.v;
  if (typeof version !== 'number' || !Number.isInteger(version)) return 'foreign';

  // The id is the only field a scope can be built from. Without it the file is not ours to
  // use, whatever its version says.
  const id = record.id;
  if (typeof id !== 'string' || !UUID.test(id)) return 'foreign';

  if (version === MARK_VERSION) {
    // `created` is informational, but it is part of the shape this build writes and reads.
    // A file missing it was not written by this App and is not overwritten.
    const created = record.created;
    if (typeof created !== 'string' || created.length === 0) return 'foreign';

    const mark: CardMark = { v: MARK_VERSION, id, created };
    // A malformed label does not make the id unusable; it is dropped rather than trusted.
    if (typeof record.label === 'string') mark.label = record.label;
    return mark;
  }

  if (version > MARK_VERSION) return 'newer';

  return 'foreign';
}

/**
 * The §4 policy table as one function.
 *
 * `newer` is unmarked here rather than adopted: this build cannot promise to honour fields
 * it does not know, and a scope is not worth a silent misreading. The card is still told
 * apart by its fingerprint, and re-marking remains an explicit act.
 */
export function decideMark(state: MarkState, options: DecideOptions): MarkDecision {
  if (typeof state === 'object' && state !== null) return { action: 'use' };

  if (state === 'absent' && options.writable) {
    return {
      action: 'create',
      write: newCardMark(options.now ?? new Date().toISOString(), options.label, options.random),
    };
  }

  // absent on a read-only card, a newer format, or something unparseable: none of them is
  // written, and all of them fall back to a fingerprint.
  return { action: 'unmarked' };
}

/**
 * The exact bytes of the file: one field order regardless of how the object was assembled,
 * no trailing newline, the optional label last. Stable JSON means a rewritten mark with the
 * same values is the same file.
 */
export function serialiseCardMark(mark: CardMark): string {
  const ordered: CardMark = { v: mark.v, id: mark.id, created: mark.created };
  if (mark.label !== undefined) ordered.label = mark.label;
  return JSON.stringify(ordered);
}

/** The scope a card's name produces: `mark:<id>`. */
export function scopeFor(mark: CardMark): string {
  return `mark:${mark.id}`;
}

/** The scope a card's contents produce when no mark is usable: `fingerprint:<sha256>`. */
export function fingerprintScope(hex: string): string {
  return `fingerprint:${hex}`;
}

/**
 * A card is a directory holding `music/`; anything else is a Folder, and a Folder is never
 * marked. Case is not information on FAT32, which is the only filesystem a card has, so
 * `Music` is the same Library as `music` and counts.
 */
export function isCardRoot(names: string[]): boolean {
  return names.some((name) => name.toLowerCase() === 'music');
}
