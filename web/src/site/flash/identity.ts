/**
 * What the board must be before anything is written (k7-site.md F4–F5).
 *
 * The rule is the chosen board's — an ESP32-S3 with 16 MB for K7, a classic ESP32 with 4 MB
 * for the K7Player — and it lives here, not in the loader, so the state machine refuses a
 * wrong board the same way whether the identity came from a real port or a test's fake one.
 */
import { FlashError } from './errors';
import type { Board } from './manifest';

export interface Identity {
  /** The chip the ROM reports, from `CHIP_NAME`: `ESP32-S3`. */
  chip: string;
  /** The flash the flash id maps to: `16MB`. */
  flashSize: string;
  /** What the browser will tell us about the port — `USB 1a86:7523`, or `unknown`. */
  port: string;
}

export function assertIdentity(identity: Identity, board: Board): void {
  if (identity.chip !== board.romChip) {
    throw new FlashError(
      'wrong-chip',
      identity.chip
        ? `The board reports ${identity.chip}. ${board.name} runs on ${board.chipProse}.`
        : `The board did not report a chip. ${board.name} runs on ${board.chipProse}.`,
    );
  }
  const flash = board.firmware.flash.size;
  if (identity.flashSize !== flash) {
    throw new FlashError(
      'wrong-flash',
      identity.flashSize && identity.flashSize !== 'unreadable'
        ? `The board has ${identity.flashSize} of flash. ${board.name} needs ${flash}.`
        : `The board's flash could not be read. ${board.name} needs ${flash}.`,
    );
  }
}
