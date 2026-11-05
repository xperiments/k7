/**
 * The Playlists the device holds, and which one the broadcast is walking.
 *
 * The page never writes a list here: a list reaches the device inside a Snapshot
 * (ADR 0051), and this module only reads what the last accepted image said and asks the
 * broadcast to choose one. `select` is the one command.
 *
 * Both links answer through the `Device` seam, so nothing here fetches; the parsers are
 * exported because the direct Device uses them, and a document this module cannot read
 * never reaches a view (O5).
 */

import { getDevice } from './device';
import { DeviceError } from './transport';

export interface DevicePlaylist {
  /** 16 hex characters. */
  id: string;
  name: string;
  /** Unix seconds. */
  created: number;
  rows: number;
}

export interface DevicePlaylists {
  /** 16 hex characters, or null when nothing has been chosen. */
  selected: string | null;
  /** Whether that choice is actually walking now. A card that is out keeps the choice —
   * it comes back with the card — while the broadcast plays the Library's order. */
  walking: boolean;
  /** Rows of the selected list the Library could not resolve. */
  missing: number;
  lists: DevicePlaylist[];
}

export interface DevicePlaylistRows {
  id: string;
  name: string;
  /** Snapshot paths, in play order. */
  rows: string[];
}

export class PlaylistsError extends Error {
  constructor(
    message: string,
    readonly status: number,
  ) {
    super(message);
    this.name = 'PlaylistsError';
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function finite(value: unknown, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

/** One entry of the index. An entry without an id or a name is not a Playlist a page can show. */
export function parsePlaylist(value: unknown): DevicePlaylist | null {
  if (!isRecord(value)) return null;
  if (typeof value.id !== 'string' || typeof value.name !== 'string') return null;
  return {
    id: value.id,
    name: value.name,
    created: finite(value.created, 0),
    rows: finite(value.rows, 0),
  };
}

/** `GET /api/playlists`: the index, tolerating the fields a firmware predates. */
export function parsePlaylists(value: unknown): DevicePlaylists | null {
  if (!isRecord(value)) return null;
  const lists: DevicePlaylist[] = [];
  if (Array.isArray(value.lists)) {
    for (const entry of value.lists) {
      const playlist = parsePlaylist(entry);
      if (playlist !== null) lists.push(playlist);
    }
  }
  return {
    selected: typeof value.selected === 'string' ? value.selected : null,
    walking: value.walking === true,
    missing: finite(value.missing, 0),
    lists,
  };
}

/** `GET /api/playlists/<id>`: one list's rows, in play order. */
export function parsePlaylistRows(value: unknown): DevicePlaylistRows | null {
  if (!isRecord(value)) return null;
  if (typeof value.id !== 'string' || typeof value.name !== 'string') return null;
  if (!Array.isArray(value.rows)) return null;
  return {
    id: value.id,
    name: value.name,
    rows: value.rows.filter((row): row is string => typeof row === 'string'),
  };
}

export interface SelectResult {
  order: 'tree' | 'list';
  selected: string | null;
}

/** `POST /api/playlists/select`: the order the broadcast is walking now. */
export function parseSelectResult(value: unknown): SelectResult | null {
  if (!isRecord(value)) return null;
  return {
    order: value.order === 'list' ? 'list' : 'tree',
    selected: typeof value.selected === 'string' ? value.selected : null,
  };
}

/**
 * The seam's `DeviceError` in this module's own vocabulary, so the pages that switch on a
 * status keep working: `refused` carries the device's HTTP status, and a link that has
 * nothing to answer for is `0` (the "did not answer" sentence).
 */
function playlistsError(error: unknown): PlaylistsError {
  if (error instanceof DeviceError) return new PlaylistsError(error.message, error.status ?? 0);
  return new PlaylistsError(error instanceof Error ? error.message : String(error), 0);
}

export async function fetchPlaylists(): Promise<DevicePlaylists> {
  try {
    return await getDevice().request('playlists');
  } catch (error) {
    throw playlistsError(error);
  }
}

export async function fetchPlaylistRows(id: string): Promise<DevicePlaylistRows> {
  try {
    return await getDevice().request('playlist', { id });
  } catch (error) {
    throw playlistsError(error);
  }
}

/** Tell the broadcast which list to walk; `null` returns it to the Library's order. */
export async function selectPlaylist(id: string | null): Promise<SelectResult> {
  try {
    return await getDevice().request('select', { id });
  } catch (error) {
    throw playlistsError(error);
  }
}
