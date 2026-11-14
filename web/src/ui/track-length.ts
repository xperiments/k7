import type { McpTrack } from '../device/mcp';
import type { ImageMeasurement } from '../domain/snapshot';
import { formatDuration } from './format';

/**
 * A track's length as the App shows it.
 *
 * The device answers from the image it last applied, so with the card in the computer there
 * is no `analysis` and the browser's own row is the only answer. A length that neither has —
 * an unmeasured track, or an image from before the format carried one — is `null`, not
 * `0:00`: a length nobody measured must not look like a very short song.
 */
export function trackLengthSeconds(
  track: McpTrack | null | undefined,
  image: ImageMeasurement | null,
): number | null {
  const ms = track?.analysis?.durationMs ?? image?.durationMs ?? null;
  if (ms === null || !Number.isFinite(ms) || ms <= 0) return null;
  return ms / 1000;
}

/** The label the row prints, or null when there is nothing honest to print. */
export function trackLengthLabel(
  track: McpTrack | null | undefined,
  image: ImageMeasurement | null,
): string | null {
  const seconds = trackLengthSeconds(track, image);
  return seconds === null ? null : formatDuration(seconds);
}
