/**
 * The seam between the App and a device.
 *
 * Every view asks the same fourteen questions, and there is one way to answer them: over the
 * local network when Chrome's Local Network Access permission allows it (see `local.ts`). A
 * window with no player answers `none` and rejects every request. The seam exists so a view
 * never knows which, and so the link and the views cannot drift in what they accept or
 * return.
 *
 * `OpName` comes from the device's operation table on purpose: a second copy of the names
 * here is a second thing to forget.
 */

import type { OpName } from './protocol';
import type { McpSearch, McpStats, McpTrack, LibraryListing, SearchArgs } from './mcp';
import type { DevicePlaylists, DevicePlaylistRows, SelectResult } from './playlists';
import type { DeviceStatus } from './status';

/**
 * Where the answers come from, or `none` when there is no player in this window.
 *
 * `local` is the App reaching the device over the LAN, gated on the Local Network Access
 * permission (`local.ts`).
 */
export type Link = 'local' | 'none';

export interface AudioState {
  playing: boolean;
  volume: number;
  /** True when the browser refused `play()`: a message is not a user gesture (O3). */
  blocked: boolean;
}

/**
 * What each operation takes. `undefined` is a real value here: an operation with no
 * arguments is called as `request('status')`, not with a fabricated empty object.
 */
export interface OpArgs {
  status: undefined;
  playlists: undefined;
  playlist: { id: string };
  select: { id: string | null };
  rescan: undefined;
  play: { path: string };
  'library.search': SearchArgs;
  'library.list': { path: string; depth?: number; limit?: number };
  'library.stats': undefined;
  'library.track': { path: string };
  'stream.play': undefined;
  'stream.pause': undefined;
  'stream.volume': { level: number };
  'stream.state': undefined;
}

/** What each operation answers, already parsed. Nothing unvalidated reaches a view. */
export interface OpData {
  status: DeviceStatus;
  playlists: DevicePlaylists;
  playlist: DevicePlaylistRows;
  select: SelectResult;
  rescan: { ok: boolean };
  play: Record<string, never>;
  'library.search': McpSearch;
  'library.list': LibraryListing;
  'library.stats': McpStats;
  'library.track': McpTrack;
  'stream.play': AudioState;
  'stream.pause': AudioState;
  'stream.volume': AudioState;
  'stream.state': AudioState;
}

/** The closed error vocabulary the device API answers with, plus `direct`'s own failures. */
export type DeviceErrorCode =
  | 'bad-message'
  | 'unknown-op'
  | 'bad-args'
  | 'too-large'
  | 'timeout'
  | 'unreachable'
  | 'refused'
  | 'blocked'
  | 'version';

export class DeviceError extends Error {
  constructor(
    message: string,
    readonly code: DeviceErrorCode,
    /** The device's HTTP status when the refusal came from it. */
    readonly status?: number,
  ) {
    super(message);
    this.name = 'DeviceError';
  }
}

export interface Device {
  /** Read, not cached: a view that renders once must still see a link that arrived later. */
  link(): Link;
  request<O extends OpName>(op: O, args?: OpArgs[O]): Promise<OpData[O]>;
  /** Notifies when `link()` would answer differently. Returns a disposer. */
  onLinkChange(listener: () => void): () => void;
}

/** An absent player is not an error: it is the standalone case, and every view has copy for it. */
export const NO_LINK = new DeviceError('no player is connected', 'unreachable');
