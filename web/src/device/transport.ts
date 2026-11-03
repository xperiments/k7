/**
 * The seam between the App and a device.
 *
 * Every view asks the same fifteen questions (bridge-protocol.md §3), and there are three
 * ways to answer them: through the Launcher's bridge, directly because this *is* the Device
 * copy, or not at all. The seam exists so a view never knows which, and so the two
 * implementations cannot drift in what they accept or return.
 *
 * `OpName` comes from the wire module on purpose: the operation table is the protocol's,
 * and a second copy of the names here is a second thing to forget.
 */

import type { OpName } from '../bridge/protocol';
import type { McpSearch, McpStats, McpTrack, LibraryListing, SearchArgs } from './mcp';
import type { DevicePlaylists, DevicePlaylistRows, SelectResult } from './playlists';
import type { DeviceStatus } from './status';
import type { UploadResult } from './upload';

/** Where the answers come from, or `none` when there is no player in this window. */
export type Link = 'bridge' | 'direct' | 'none';

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
  snapshot: { bytes: Uint8Array<ArrayBuffer> };
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
  snapshot: UploadResult;
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

/** The closed error vocabulary of bridge-protocol.md §4, plus `direct`'s own failures. */
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
