/**
 * The boards the Site can flash, each from the manifest its release wrote (k7-site.md F9).
 *
 * There is no constant here for a version, an offset, a size or a hash: `web/firmware.json` is
 * the K7's truth and `web/firmware-player.json` the K7Player's, and the build cannot offer a
 * release the repository does not carry. What is here is the page's own knowledge: what the
 * person calls each board, which chip it must be, and where its parts are served.
 */
import k7Data from '../../../firmware.json';
import playerData from '../../../firmware-player.json';

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

export type BoardId = 'k7' | 'player';

export interface Board {
  id: BoardId;
  /** What the person calls it. */
  name: string;
  /** One sentence: what it is and what it is for. */
  summary: string;
  /** The hardware, in the words the card's description uses. */
  hardware: string;
  /** The chip the ROM reports (`CHIP_NAME`). */
  romChip: string;
  /** The same chip in prose, with its article: "an ESP32-S3", "a classic ESP32". */
  chipProse: string;
  /** The same chip as esptool names it on a command line. */
  esptoolChip: string;
  /** Where the parts are served: the K7 under the version alone, the player under its own. */
  basePath: string;
  firmware: Firmware;
}

const K7: Firmware = k7Data as Firmware;
const PLAYER: Firmware = playerData as Firmware;

/**
 * The order is the page's: the K7 first, because it is the board the product is, and the
 * player second, because it is useless without one.
 */
export const BOARDS: readonly Board[] = [
  {
    id: 'k7',
    name: 'K7',
    summary: 'The station: the screen, the card slot and the radio that broadcasts.',
    hardware: 'the Sunton 8048S050C — an ESP32-S3 with 16 MB of flash',
    romChip: 'ESP32-S3',
    chipProse: 'an ESP32-S3',
    esptoolChip: 'esp32s3',
    basePath: '/firmware',
    firmware: K7,
  },
  {
    id: 'player',
    name: 'K7Player',
    summary: 'The player: a small board that puts the K7’s stream on a line output.',
    hardware: 'a classic ESP32 with 4 MB of flash',
    romChip: 'ESP32',
    chipProse: 'a classic ESP32',
    esptoolChip: 'esp32',
    basePath: '/firmware/player',
    firmware: PLAYER,
  },
];

export function boardById(id: BoardId): Board {
  return BOARDS.find((board) => board.id === id) ?? BOARDS[0]!;
}

/** `0x10000` — an offset as the bootloader and the command line write it. */
export function hex(offset: number): string {
  return `0x${offset.toString(16)}`;
}

/** Where the Site serves a part: the version is a directory, so a release is a new URL. */
export function partUrl(board: Board, part: FirmwarePart): string {
  return `${board.basePath}/${board.firmware.version}/${part.file}`;
}

/**
 * The same parts, for a person who has esptool and no Web Serial. It is generated from the
 * board's manifest so a hand-written command cannot drift from the bytes (k7-site.md R6).
 */
export function manualCommand(board: Board, port = '/dev/ttyUSB0'): string {
  const files = board.firmware.parts
    .map((part) => `${hex(part.offset)} ${part.file}`)
    .join(' ');
  return (
    `esptool --chip ${board.esptoolChip} --port ${port} --baud 460800 write-flash ` +
    `--flash-mode ${board.firmware.flash.mode} --flash-size ${board.firmware.flash.size} ` +
    `--flash-freq ${board.firmware.flash.freq} ${files}`
  );
}

/** The total the progress bar counts toward: the bytes of the board's parts. */
export function partsTotal(board: Board): number {
  return board.firmware.parts.reduce((sum, part) => sum + part.size, 0);
}
