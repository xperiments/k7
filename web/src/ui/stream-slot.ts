import type { DeviceStatus } from '../device/status';

/**
 * What a live stream's error means from the page's side.
 *
 * `busy` is the station saying "no": it carries one listener, and another page has it. That
 * is not a stream that stopped, and the page must not tell the listener it did (EN-09).
 * `drop` is everything else - a link that died, a player that rebooted, a device that does
 * not report its slots at all - and it is recovered by reconnecting as the page always has.
 */
export type StreamFailure = 'busy' | 'drop';

/**
 * The one fact that separates the two: whether this page ever played the stream. A refusal
 * arrives before the first `play` event, so a stream that never started and finds the slots
 * full was refused; a stream that had been playing lost its link, even if its own slot is
 * still counted for the instant before the device reclaims it.
 *
 * `status` is null when the device could not be asked, which falls back to `drop`: waiting
 * for a slot that might be free would hide a real failure.
 */
export function classifyStreamFailure(
  playedBefore: boolean,
  status: DeviceStatus | null,
): StreamFailure {
  if (playedBefore) return 'drop';
  const slots = status?.listeners;
  if (!slots || slots.slots <= 0) return 'drop';
  return slots.used >= slots.slots ? 'busy' : 'drop';
}
