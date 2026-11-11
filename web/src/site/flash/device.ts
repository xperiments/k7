/**
 * The device, behind a seam.
 *
 * `esptool-js` is the protocol; `FlashDevice` is the four things the page needs from a board
 * — who it is, write these parts, close — so the state machine can be driven by a fake in a
 * test and by a real serial port in a browser without changing a line of the rules.
 */
import {
  ESPLoader,
  Transport,
  type FlashFreqValues,
  type FlashModeValues,
  type FlashSizeValues,
} from 'esptool-js';

import { FlashError } from './errors';
import { assertIdentity, type Identity } from './identity';
import { FIRMWARE } from './manifest';
import { resetIntoApp } from './reset';

export interface FlashPart {
  data: Uint8Array;
  address: number;
}

export interface WriteOptions {
  erase: boolean;
  onProgress?: (written: number, total: number) => void;
}

export interface FlashDevice {
  readonly identity: Identity;
  write(parts: FlashPart[], options: WriteOptions): Promise<void>;
  close(): Promise<void>;
}

export type OpenDevice = () => Promise<FlashDevice>;

/** The browser does not expose a path, only the bridge's USB ids. That is what it can say. */
function portLabel(port: SerialPort): string {
  const info = port.getInfo?.() ?? {};
  const id = (value?: number): string =>
    value === undefined ? '----' : value.toString(16).padStart(4, '0');
  return `USB ${id(info.usbVendorId)}:${id(info.usbProductId)}`;
}

/** An esptool failure, in the words the page can act on (k7-site.md F8). */
function failure(error: unknown): FlashError {
  if (error instanceof FlashError) return error;
  const message = error instanceof Error ? error.message : String(error);
  if (/Failed to connect|failed to connect|Timed out|timeout/i.test(message)) {
    return new FlashError('boot-mode', message);
  }
  if (/already open|NetworkError|Failed to open|Failed to claim|Unable to claim/i.test(message)) {
    return new FlashError('port-busy', message);
  }
  if (/permission|NotAllowedError|SecurityError/i.test(message)) {
    return new FlashError('permission', message);
  }
  return new FlashError('write', message);
}

/**
 * Ask the browser for a port, connect, and report who is on the other end. Nothing is
 * written here: the confirmation is the machine's job (k7-site.md F6).
 */
export async function openSerialDevice(): Promise<FlashDevice> {
  const serial = navigator.serial;
  if (!serial) {
    throw new FlashError('no-port', 'This browser has no Web Serial. Use Chrome or Edge.');
  }

  let port: SerialPort;
  try {
    port = await serial.requestPort();
  } catch {
    throw new FlashError('cancelled', 'No port was chosen.');
  }

  const transport = new Transport(port);
  let loader: ESPLoader;
  try {
    loader = new ESPLoader({ transport, baudrate: 460800 });
    await loader.main();
  } catch (error) {
    await transport.disconnect().catch(() => undefined);
    throw failure(error);
  }

  const chip = loader.chip?.CHIP_NAME ?? '';
  let flashSize = 'unreadable';
  try {
    flashSize = (await loader.detectFlashSize()) ?? 'unreadable';
  } catch {
    /* the identity check below refuses it, with the sentence a person can read */
  }

  const identity: Identity = { chip, flashSize, port: portLabel(port) };
  try {
    assertIdentity(identity);
  } catch (error) {
    await transport.disconnect().catch(() => undefined);
    throw error;
  }

  return {
    identity,

    async write(parts, { erase, onProgress }) {
      try {
        await loader.writeFlash({
          fileArray: parts,
          flashMode: FIRMWARE.flash.mode as FlashModeValues,
          flashFreq: FIRMWARE.flash.freq as FlashFreqValues,
          flashSize: FIRMWARE.flash.size as FlashSizeValues,
          eraseAll: erase,
          compress: true,
          reportProgress: (_file, written, total) => onProgress?.(written, total),
        });
      } catch (error) {
        throw failure(error);
      }
    },

    async close() {
      /* An edge, not a level: the flash left RTS released, and esptool-js's own hard reset
       * only releases it again (reset.ts). Without this the board stays in the stub. */
      try {
        await resetIntoApp(transport);
      } catch {
        /* the cable went, or the port refused; the disconnect below is still right */
      }
      await transport.disconnect().catch(() => undefined);
    },
  };
}
