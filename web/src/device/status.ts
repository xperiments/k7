/**
 * The device's Card state, as `/api/status` reports it. Nothing here requires a device:
 * an absent device is simply `null`, which is the local `npm run dev` situation and must
 * not be rendered as an error.
 */
export interface PlayingStatus {
  title?: string;
  path?: string;
  elapsed?: number;
  duration?: number;
}

export interface PlayingPlaylist {
  /** 16 hex characters. */
  id: string;
  name: string;
}

export interface DeviceStatus {
  card: boolean;
  tracks: number;
  generation: number;
  library: string;
  /** Which order the broadcast is walking: the library tree, or a Playlist. */
  order: 'tree' | 'list';
  /** The Playlist being walked, or null when the library's own order is playing. */
  playlist: PlayingPlaylist | null;
  playing: PlayingStatus | null;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function finiteNumber(value: unknown): number | undefined {
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined;
}

/**
 * `playing` is a placeholder shape today; an unrecognised object is kept as an empty one
 * rather than throwing the whole status away, because the Card state is the fact the view
 * selection needs. A missing or malformed `playing` becomes `null`.
 */
function parsePlaylist(value: unknown): PlayingPlaylist | null {
  if (!isRecord(value)) return null;
  if (typeof value.id !== 'string' || typeof value.name !== 'string') return null;
  return { id: value.id, name: value.name };
}

function parsePlaying(value: unknown): PlayingStatus | null {
  if (!isRecord(value)) return null;

  const playing: PlayingStatus = {};
  if (typeof value.title === 'string') playing.title = value.title;
  if (typeof value.path === 'string') playing.path = value.path;
  /* The wire says `_ms`, because the device reports milliseconds; the view formats them. */
  const elapsed = finiteNumber(value.elapsed_ms);
  const duration = finiteNumber(value.duration_ms);
  if (elapsed !== undefined) playing.elapsed = elapsed;
  if (duration !== undefined) playing.duration = duration;
  return playing;
}

/**
 * The contract the firmware promises, checked at the boundary. A malformed body returns
 * `null`, which the UI treats as "no device": one wrong response must not throw away the
 * page or produce an error banner.
 */
export function parseStatus(value: unknown): DeviceStatus | null {
  if (!isRecord(value)) return null;

  if (typeof value.card !== 'boolean') return null;
  if (typeof value.library !== 'string') return null;

  const tracks = finiteNumber(value.tracks);
  const generation = finiteNumber(value.generation);
  if (tracks === undefined || generation === undefined) return null;

  /* A firmware that predates the order fields reports neither; the library's order is
   * then the truthful reading, not an error. */
  const playlist = parsePlaylist(value.playlist);
  return {
    card: value.card,
    tracks,
    generation,
    library: value.library,
    order: value.order === 'list' && playlist ? 'list' : 'tree',
    playlist,
    playing: parsePlaying(value.playing),
  };
}

export function parseStatusText(text: string): DeviceStatus | null {
  try {
    return parseStatus(JSON.parse(text) as unknown);
  } catch {
    return null;
  }
}
