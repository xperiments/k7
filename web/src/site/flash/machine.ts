/**
 * The flash as a state machine (k7-site.md F8), with the board and the parts behind seams.
 *
 * The rules are here, not in the page: the board must be the chip and the flash size the
 * chosen board needs, nothing is written until the confirmation, its parts go at their own
 * offsets in order, and an erase happens only when it was asked for. One machine serves one
 * board: a different board is a different release and a different identity. The tests drive
 * this with a fake device and a fake download; the browser drives it with Web Serial and the
 * Site's own parts.
 */
import type { FlashDevice, FlashPart, OpenDevice } from './device';
import { failureOf, type FailureCause } from './errors';
import { assertIdentity, type Identity } from './identity';
import { partsTotal, type Board, type FirmwarePart } from './manifest';

export type Step =
  | 'idle'
  | 'opening'
  | 'ready'
  | 'downloading'
  | 'writing'
  | 'done'
  | 'failed';

export interface MachineState {
  step: Step;
  version: string;
  identity?: Identity;
  erase: boolean;
  written: number;
  total: number;
  cause?: FailureCause;
  message?: string;
}

export interface FlashDeps {
  openDevice: OpenDevice;
  download: (board: Board, part: FirmwarePart) => Promise<Uint8Array>;
}

export function initialState(board: Board): MachineState {
  return { step: 'idle', version: board.firmware.version, erase: false, written: 0, total: 0 };
}

export class FlashMachine {
  private device: FlashDevice | null = null;
  private state: MachineState;

  constructor(
    private readonly board: Board,
    private readonly deps: FlashDeps,
    private readonly onChange: (state: MachineState) => void,
  ) {
    this.state = initialState(board);
    this.onChange(this.state);
  }

  get current(): MachineState {
    return this.state;
  }

  private set(patch: Partial<MachineState>): void {
    this.state = { ...this.state, ...patch };
    this.onChange(this.state);
  }

  /** Open the port and say who is on the other end. Nothing is written. */
  async prepare(): Promise<void> {
    const busy = this.state.step;
    if (busy === 'opening' || busy === 'ready' || busy === 'downloading' || busy === 'writing') {
      return;
    }

    this.set({ step: 'opening', cause: undefined, message: undefined, written: 0, total: 0 });
    try {
      const device = await this.deps.openDevice(this.board);
      try {
        assertIdentity(device.identity, this.board);
      } catch (error) {
        await device.close().catch(() => undefined);
        throw error;
      }
      this.device = device;
      this.set({ step: 'ready', identity: device.identity });
    } catch (error) {
      const { cause, message } = failureOf(error);
      this.set(cause === 'cancelled' ? { step: 'idle' } : { step: 'failed', cause, message });
    }
  }

  /** Download, verify, then write. Called only from the ready state, i.e. after a person
   * saw the board and the version and pressed the control. */
  async flash(options: { erase: boolean }): Promise<void> {
    const device = this.device;
    if (this.state.step !== 'ready' || device === null) return;

    this.set({
      step: 'downloading',
      erase: options.erase,
      written: 0,
      total: partsTotal(this.board),
    });
    try {
      const parts: FlashPart[] = [];
      for (const part of this.board.firmware.parts) {
        parts.push({ data: await this.deps.download(this.board, part), address: part.offset });
      }

      this.set({
        step: 'writing',
        total: parts.reduce((sum, part) => sum + part.data.length, 0),
      });
      await device.write(parts, {
        erase: options.erase,
        onProgress: (written, total) => this.set({ written, total }),
      });
      this.set({ step: 'done' });
    } catch (error) {
      const { cause, message } = failureOf(error);
      this.set({ step: 'failed', cause, message });
    } finally {
      this.device = null;
      await device.close().catch(() => undefined);
    }
  }

  /** Back to the start, closing a port that was opened but never used. */
  async reset(): Promise<void> {
    const device = this.device;
    this.device = null;
    await device?.close().catch(() => undefined);
    this.state = initialState(this.board);
    this.onChange(this.state);
  }
}
