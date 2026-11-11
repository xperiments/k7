import { describe, expect, it } from 'vitest';

import { canFlash } from './capability';
import { FIRMWARE, manualCommand, partUrl } from './manifest';

describe('the release the Site offers', () => {
  it('is the version and the offsets the manifest carries', () => {
    expect(FIRMWARE.version).toMatch(/^\d+\.\d+\.\d+$/);
    expect(FIRMWARE.parts.map((part) => part.offset)).toEqual([0x0, 0x8000, 0x10000]);
    expect(FIRMWARE.parts.map((part) => part.file)).toEqual([
      'bootloader.bin',
      'partition-table.bin',
      'sd_stream_server.bin',
    ]);
  });

  it('serves each part from its versioned directory', () => {
    expect(partUrl(FIRMWARE.parts[0]!)).toBe(
      `/firmware/${FIRMWARE.version}/bootloader.bin`,
    );
  });

  it('writes a manual command with the manifest offsets, not a constant', () => {
    const command = manualCommand('/dev/ttyUSB0');
    expect(command).toContain(`0x0 bootloader.bin`);
    expect(command).toContain(`0x8000 partition-table.bin`);
    expect(command).toContain(`0x10000 sd_stream_server.bin`);
    expect(command).toContain(`--flash-size ${FIRMWARE.flash.size}`);
    expect(command).toContain('--chip esp32s3');
  });
});

describe('the flash capability', () => {
  it('is false without Web Serial and true with a port chooser', () => {
    expect(canFlash({})).toBe(false);
    expect(canFlash({ serial: {} })).toBe(false);
    expect(canFlash({ serial: { requestPort: () => undefined } })).toBe(true);
  });
});
