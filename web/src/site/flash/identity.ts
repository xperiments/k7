/**
 * What the board must be before anything is written (k7-site.md F4–F5).
 *
 * The rule lives here, not in the loader, so the state machine refuses a wrong board the
 * same way whether the identity came from a real port or a test's fake one.
 */
import { FlashError } from './errors';

export interface Identity {
  /** The chip the ROM reports, from `CHIP_NAME`: `ESP32-S3`. */
  chip: string;
  /** The flash the flash id maps to: `16MB`. */
  flashSize: string;
  /** What the browser will tell us about the port — `USB 1a86:7523`, or `unknown`. */
  port: string;
}

export const REQUIRED_CHIP = 'ESP32-S3';
export const REQUIRED_FLASH = '16MB';

export function assertIdentity(identity: Identity): void {
  if (identity.chip !== REQUIRED_CHIP) {
    throw new FlashError(
      'wrong-chip',
      identity.chip
        ? `The board reports ${identity.chip}. K7 needs an ${REQUIRED_CHIP}.`
        : `The board did not report a chip. K7 needs an ${REQUIRED_CHIP}.`,
    );
  }
  if (identity.flashSize !== REQUIRED_FLASH) {
    throw new FlashError(
      'wrong-flash',
      identity.flashSize && identity.flashSize !== 'unreadable'
        ? `The board has ${identity.flashSize} of flash. K7 needs ${REQUIRED_FLASH}.`
        : `The board's flash could not be read. K7 needs ${REQUIRED_FLASH}.`,
    );
  }
}
