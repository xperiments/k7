import { describe, expect, it } from 'vitest';

import { canFlash } from './capability';
import { BOARDS, boardById, manualCommand, partUrl } from './manifest';

describe('the releases the Site offers', () => {
  it('offers the two boards, in the order the product has them', () => {
    expect(BOARDS.map((board) => board.id)).toEqual(['k7', 'player']);
    expect(boardById('player').name).toBe('K7Player');
  });

  it('takes K7\'s version, offsets and parts from its manifest', () => {
    const { firmware } = boardById('k7');
    expect(firmware.version).toMatch(/^\d+\.\d+\.\d+$/);
    expect(firmware.parts.map((part) => part.offset)).toEqual([0x0, 0x8000, 0x10000]);
    expect(firmware.parts.map((part) => part.file)).toEqual([
      'bootloader.bin',
      'partition-table.bin',
      'sd_stream_server.bin',
    ]);
  });

  it('takes the K7Player\'s from its own, with the bootloader at 0x1000', () => {
    const { firmware } = boardById('player');
    expect(firmware.version).toMatch(/^\d+\.\d+\.\d+$/);
    expect(firmware.parts.map((part) => part.offset)).toEqual([0x1000, 0x8000, 0x10000]);
    expect(firmware.parts.map((part) => part.file)).toEqual([
      'bootloader.bin',
      'partition-table.bin',
      'k7player.bin',
    ]);
    expect(firmware.chip).toBe('esp32');
    expect(firmware.flash).toEqual({ mode: 'dio', size: '4MB', freq: '40m' });
  });

  it('serves each board\'s parts from its own directory', () => {
    const k7 = boardById('k7');
    const player = boardById('player');
    expect(partUrl(k7, k7.firmware.parts[0]!)).toBe(
      `/firmware/${k7.firmware.version}/bootloader.bin`,
    );
    expect(partUrl(player, player.firmware.parts[0]!)).toBe(
      `/firmware/player/${player.firmware.version}/bootloader.bin`,
    );
  });

  it('writes a manual command with the board\'s offsets and chip, not a constant', () => {
    const k7 = manualCommand(boardById('k7'), '/dev/ttyUSB0');
    expect(k7).toContain('0x0 bootloader.bin');
    expect(k7).toContain('0x8000 partition-table.bin');
    expect(k7).toContain('0x10000 sd_stream_server.bin');
    expect(k7).toContain('--chip esp32s3');
    expect(k7).toContain('--flash-size 16MB');

    const player = manualCommand(boardById('player'), '/dev/ttyUSB1');
    expect(player).toContain('--chip esp32');
    expect(player).toContain('0x1000 bootloader.bin');
    expect(player).toContain('0x10000 k7player.bin');
    expect(player).toContain('--flash-size 4MB');
    expect(player).toContain('--port /dev/ttyUSB1');
  });
});

describe('the flash capability', () => {
  it('is false without Web Serial and true with a port chooser', () => {
    expect(canFlash({})).toBe(false);
    expect(canFlash({ serial: {} })).toBe(false);
    expect(canFlash({ serial: { requestPort: () => undefined } })).toBe(true);
  });
});
