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

export interface DeviceStatus {
  card: boolean;
  tracks: number;
  generation: number;
  library: string;
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
function parsePlaying(value: unknown): PlayingStatus | null {
  if (!isRecord(value)) return null;

  const playing: PlayingStatus = {};
  if (typeof value.title === 'string') playing.title = value.title;
  if (typeof value.path === 'string') playing.path = value.path;
  const elapsed = finiteNumber(value.elapsed);
  const duration = finiteNumber(value.duration);
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

  return {
    card: value.card,
    tracks,
    generation,
    library: value.library,
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
