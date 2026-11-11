/**
 * The release the Site offers, read from the file the release action wrote (k7-site.md §4).
 *
 * There is no constant here for a version, an offset or a hash: `web/firmware.json` is the
 * one truth, and the build cannot offer a release the repository does not carry.
 */
import data from '../../../firmware.json';

export interface FirmwarePart {
  file: string;
  offset: number;
  size: number;
  sha256: string;
}

export interface Firmware {
  version: string;
  commit: string;
  idf: string;
  chip: string;
  flash: { mode: string; size: string; freq: string };
  built_at: string;
  parts: FirmwarePart[];
}

export const FIRMWARE = data as Firmware;

/** `0x10000` — an offset as the bootloader and the command line write it. */
export function hex(offset: number): string {
  return `0x${offset.toString(16)}`;
}

/** Where the Site serves a part: the version is a directory, so a release is a new URL. */
export function partUrl(part: FirmwarePart): string {
  return `/firmware/${FIRMWARE.version}/${part.file}`;
}

/**
 * The same three parts, for a person who has esptool and no Web Serial. It is generated from
 * the manifest so a hand-written command cannot drift from the bytes (k7-site.md R6).
 */
export function manualCommand(port = '/dev/ttyUSB0'): string {
  const files = FIRMWARE.parts.map((part) => `${hex(part.offset)} ${part.file}`).join(' ');
  return (
    `esptool --chip esp32s3 --port ${port} --baud 460800 write-flash ` +
    `--flash-mode ${FIRMWARE.flash.mode} --flash-size ${FIRMWARE.flash.size} ` +
    `--flash-freq ${FIRMWARE.flash.freq} ${files}`
  );
}

/** The total the progress bar counts toward: the bytes of the three parts. */
export function partsTotal(): number {
  return FIRMWARE.parts.reduce((sum, part) => sum + part.size, 0);
}
