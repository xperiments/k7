/**
 * The Playlists the device holds, and which one the broadcast is walking.
 *
 * The page never writes a list here: a list reaches the device inside a Snapshot
 * (ADR 0051), and this module only reads what the last accepted image said and asks the
 * broadcast to choose one. `select` is the one command.
 */

export interface DevicePlaylist {
  /** 16 hex characters. */
  id: string;
  name: string;
  /** Unix seconds. */
  created: number;
  rows: number;
}

export interface DevicePlaylists {
  /** 16 hex characters, or null when the Library's own order is playing. */
  selected: string | null;
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

async function read<T>(path: string): Promise<T> {
  let response: Response;
  try {
    response = await fetch(path, { cache: 'no-store', headers: { accept: 'application/json' } });
  } catch {
    throw new PlaylistsError('the device did not answer', 0);
  }
  const text = await response.text();
  if (!response.ok) {
    let detail = text;
    try {
      const parsed = JSON.parse(text) as { error?: string };
      if (parsed.error) detail = parsed.error;
    } catch {
      /* the body is the device's error page */
    }
    throw new PlaylistsError(detail || `the device answered ${response.status}`, response.status);
  }
  return JSON.parse(text) as T;
}

export async function fetchPlaylists(): Promise<DevicePlaylists> {
  const body = await read<Partial<DevicePlaylists>>('/api/playlists');
  return {
    selected: typeof body.selected === 'string' ? body.selected : null,
    missing: typeof body.missing === 'number' ? body.missing : 0,
    lists: Array.isArray(body.lists) ? body.lists : [],
  };
}

export async function fetchPlaylistRows(id: string): Promise<DevicePlaylistRows> {
  return read<DevicePlaylistRows>(`/api/playlists/${encodeURIComponent(id)}`);
}

export interface SelectResult {
  order: 'tree' | 'list';
  selected: string | null;
}

/** Tell the broadcast which list to walk; `null` returns it to the Library's order. */
export async function selectPlaylist(id: string | null): Promise<SelectResult> {
  let response: Response;
  try {
    response = await fetch('/api/playlists/select', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ id }),
    });
  } catch {
    throw new PlaylistsError('the device did not answer', 0);
  }
  const text = await response.text();
  let document: Partial<SelectResult> & { error?: string } = {};
  try {
    document = text ? (JSON.parse(text) as typeof document) : {};
  } catch {
    /* the body is the device's error page; the status says enough */
  }
  if (!response.ok) {
    // 409 is the interesting one: the list has no Track the Library can find.
    throw new PlaylistsError(document.error ?? `the device refused it (${response.status})`,
      response.status);
  }
  return {
    order: document.order === 'list' ? 'list' : 'tree',
    selected: typeof document.selected === 'string' ? document.selected : null,
  };
}
