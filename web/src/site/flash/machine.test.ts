import { describe, expect, it } from 'vitest';

import type { FlashDevice, FlashPart, WriteOptions } from './device';
import { FlashError } from './errors';
import type { Identity } from './identity';
import { boardById, type Board } from './manifest';
import { FlashMachine, type MachineState } from './machine';

const K7: Board = boardById('k7');
const PLAYER: Board = boardById('player');
const GOOD: Identity = { chip: 'ESP32-S3', flashSize: '16MB', port: 'USB 1a86:7523' };
const GOOD_PLAYER: Identity = { chip: 'ESP32', flashSize: '4MB', port: 'USB 1a86:7523' };

interface Recorder {
  writes: { parts: FlashPart[]; options: WriteOptions }[];
  closes: number;
}

function fakeDevice(identity: Identity, log: Recorder, fail?: FlashError): FlashDevice {
  return {
    identity,
    async write(parts, options) {
      if (fail) throw fail;
      log.writes.push({ parts, options });
      options.onProgress?.(123, 456);
    },
    async close() {
      log.closes += 1;
    },
  };
}

function machineFor(
  device: FlashDevice | (() => Promise<FlashDevice>),
  board: Board = K7,
) {
  const states: MachineState[] = [];
  const downloaded: string[] = [];
  const machine = new FlashMachine(
    board,
    {
      openDevice: typeof device === 'function' ? device : async () => device,
      download: async (_board, part) => {
        downloaded.push(part.file);
        return new Uint8Array(1);
      },
    },
    (state) => states.push(state),
  );
  return { machine, states, downloaded };
}

describe('the flash machine', () => {
  it('refuses a board that is not an ESP32-S3, and closes it', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const device = fakeDevice({ ...GOOD, chip: 'ESP32-C3' }, recorder);
    const { machine } = machineFor(device);

    await machine.prepare();

    expect(machine.current.step).toBe('failed');
    expect(machine.current.cause).toBe('wrong-chip');
    expect(recorder.writes).toHaveLength(0);
    expect(recorder.closes).toBe(1);
  });

  it('refuses a module smaller than 16 MB', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const { machine } = machineFor(fakeDevice({ ...GOOD, flashSize: '8MB' }, recorder));

    await machine.prepare();

    expect(machine.current.step).toBe('failed');
    expect(machine.current.cause).toBe('wrong-flash');
    expect(recorder.writes).toHaveLength(0);
  });

  it('goes back to idle when the port chooser is dismissed', async () => {
    const { machine } = machineFor(async () => {
      throw new FlashError('cancelled', 'No port was chosen.');
    });

    await machine.prepare();

    expect(machine.current.step).toBe('idle');
  });

  it('writes nothing before the confirmation', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const { machine, downloaded } = machineFor(fakeDevice(GOOD, recorder));

    await machine.prepare();

    expect(machine.current.step).toBe('ready');
    expect(machine.current.identity).toEqual(GOOD);
    expect(machine.current.version).toBe(K7.firmware.version);
    expect(recorder.writes).toHaveLength(0);
    expect(downloaded).toHaveLength(0);
  });

  it('writes the manifest parts, in order, at their offsets, and closes the port', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const { machine, downloaded } = machineFor(fakeDevice(GOOD, recorder));

    await machine.prepare();
    await machine.flash({ erase: false });

    expect(downloaded).toEqual(K7.firmware.parts.map((part) => part.file));
    expect(recorder.writes).toHaveLength(1);
    expect(recorder.writes[0]?.parts.map((part) => part.address)).toEqual(
      K7.firmware.parts.map((part) => part.offset),
    );
    expect(recorder.writes[0]?.options.erase).toBe(false);
    expect(machine.current.step).toBe('done');
    expect(recorder.closes).toBe(1);
  });

  it('erases only when it was asked for', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const { machine } = machineFor(fakeDevice(GOOD, recorder));

    await machine.prepare();
    await machine.flash({ erase: true });

    expect(recorder.writes[0]?.options.erase).toBe(true);
  });

  it('refuses a part that fails its hash before writing anything', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const states: MachineState[] = [];
    const machine = new FlashMachine(
      K7,
      {
        openDevice: async () => fakeDevice(GOOD, recorder),
        download: async () => {
          throw new FlashError('hash', 'sd_stream_server.bin does not match.');
        },
      },
      (state) => states.push(state),
    );

    await machine.prepare();
    await machine.flash({ erase: false });

    expect(machine.current.step).toBe('failed');
    expect(machine.current.cause).toBe('hash');
    expect(recorder.writes).toHaveLength(0);
    expect(recorder.closes).toBe(1);
  });

  it('reports write failures with their cause and still closes the port', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const { machine } = machineFor(
      fakeDevice(GOOD, recorder, new FlashError('port-busy', 'The port is already open.')),
    );

    await machine.prepare();
    await machine.flash({ erase: false });

    expect(machine.current.step).toBe('failed');
    expect(machine.current.cause).toBe('port-busy');
    expect(recorder.closes).toBe(1);
  });

  it('passes progress through', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const { machine } = machineFor(fakeDevice(GOOD, recorder));

    await machine.prepare();
    await machine.flash({ erase: false });

    expect(machine.current.written).toBe(123);
    expect(machine.current.total).toBe(456);
  });

  /* The second board is the reason this file has a `board`: the same rules, another chip and
   * another flash, and a K7 module must be refused for it exactly as a player module is
   * refused for the K7. */
  it('accepts a classic ESP32 with 4 MB for the K7Player', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const { machine, downloaded } = machineFor(fakeDevice(GOOD_PLAYER, recorder), PLAYER);

    await machine.prepare();
    await machine.flash({ erase: false });

    expect(machine.current.step).toBe('done');
    expect(machine.current.version).toBe(PLAYER.firmware.version);
    expect(downloaded).toEqual(PLAYER.firmware.parts.map((part) => part.file));
    expect(recorder.writes[0]?.parts.map((part) => part.address)).toEqual(
      PLAYER.firmware.parts.map((part) => part.offset),
    );
  });

  it('refuses an ESP32-S3 for the K7Player, naming the board', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const { machine } = machineFor(fakeDevice(GOOD, recorder), PLAYER);

    await machine.prepare();

    expect(machine.current.cause).toBe('wrong-chip');
    expect(machine.current.message).toContain('K7Player');
    expect(recorder.writes).toHaveLength(0);
  });

  it('refuses 16 MB of flash for the K7Player', async () => {
    const recorder: Recorder = { writes: [], closes: 0 };
    const { machine } = machineFor(fakeDevice({ ...GOOD_PLAYER, flashSize: '16MB' }, recorder), PLAYER);

    await machine.prepare();

    expect(machine.current.cause).toBe('wrong-flash');
  });
});
