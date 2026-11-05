/**
 * The device's operation table: the names a transport may ask and how long each answer may
 * take.
 *
 * This file is the App's side of the device API and holds no DOM, no `fetch` and no state,
 * so the tests, the App at its HTTPS origin and the local transport all read the same
 * table. It is not a wire format: the request itself is made by `direct.ts` (and, over the
 * Local Network Access permission, by `local.ts`), and this table only says what an
 * operation is called and how long the caller waits.
 */

/** The fourteen operations. */
export type OpName =
  | 'status'
  | 'playlists'
  | 'playlist'
  | 'select'
  | 'rescan'
  | 'play'
  | 'library.search'
  | 'library.list'
  | 'library.stats'
  | 'library.track'
  | 'stream.play'
  | 'stream.pause'
  | 'stream.volume'
  | 'stream.state';

/** What the table says about one operation. */
export interface OpSpec {
  /** How long the caller waits for this op's answer, in milliseconds. */
  readonly timeout: number;
}

export const OP_SPECS: Readonly<Record<OpName, OpSpec>> = {
  status: { timeout: 4_000 },
  playlists: { timeout: 6_000 },
  playlist: { timeout: 6_000 },
  select: { timeout: 10_000 },
  rescan: { timeout: 10_000 },
  play: { timeout: 10_000 },
  'library.search': { timeout: 10_000 },
  'library.list': { timeout: 10_000 },
  'library.stats': { timeout: 10_000 },
  'library.track': { timeout: 10_000 },
  'stream.play': { timeout: 2_000 },
  'stream.pause': { timeout: 2_000 },
  'stream.volume': { timeout: 2_000 },
  'stream.state': { timeout: 2_000 },
};

const OP_NAME_SET: ReadonlySet<string> = new Set(Object.keys(OP_SPECS));

/** Whether a value is one of the operations above, and nothing else. */
export function isOpName(value: unknown): value is OpName {
  return typeof value === 'string' && OP_NAME_SET.has(value);
}
