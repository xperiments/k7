/**
 * The firmware release is a version, a manifest and three parts (k7-site.md R3–R5).
 *
 * This is the CI check: the Site offers a flash whose bytes are the bytes the manifest
 * hashed, at the offsets the flasher writes, for the version the image carries. A release
 * that forgot a part, or a part that was rebuilt after the manifest was written, fails here
 * rather than on somebody's board.
 */
import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

const here = dirname(fileURLToPath(import.meta.url));
const web = resolve(here, '..');
const root = resolve(web, '..');

interface Part {
  file: string;
  offset: number;
  size: number;
  sha256: string;
}

interface Firmware {
  version: string;
  commit: string;
  idf: string;
  chip: string;
  flash: { mode: string; size: string; freq: string };
  built_at: string;
  parts: Part[];
}

const versionFile = resolve(root, 'VERSION');
const manifestFile = resolve(web, 'firmware.json');

const version = existsSync(versionFile)
  ? readFileSync(versionFile, 'utf8').trim()
  : null;
const manifest = existsSync(manifestFile)
  ? (JSON.parse(readFileSync(manifestFile, 'utf8')) as Firmware)
  : null;

/** The manifest, or a failure that says how to make one. */
function release(): Firmware {
  expect(manifest, 'web/firmware.json — run `./tools/build.sh sunton release`').not.toBeNull();
  return manifest as Firmware;
}

describe('the firmware release', () => {
  it('has a VERSION and a manifest that agree', () => {
    expect(version, 'VERSION at the repository root').not.toBeNull();
    expect(version).toMatch(/^\d+\.\d+\.\d+$/);
    expect(release().version).toBe(version);
  });

  it('offers exactly the three parts the flasher writes', () => {
    const firmware = release();
    expect(firmware.parts.map((part) => part.offset)).toEqual([0x0, 0x8000, 0x10000]);
    expect(firmware.parts.map((part) => part.file)).toEqual([
      'bootloader.bin',
      'partition-table.bin',
      'sd_stream_server.bin',
    ]);
    expect(firmware.chip).toBe('esp32s3');
    expect(firmware.flash).toEqual({ mode: 'dio', size: '16MB', freq: '80m' });
  });

  it('has one part for every part whose bytes the manifest hashed', () => {
    const firmware = release();
    for (const part of firmware.parts) {
      const path = resolve(web, 'public', 'firmware', firmware.version, part.file);
      expect(
        existsSync(path),
        `${part.file} in web/public/firmware/${firmware.version}/`,
      ).toBe(true);
      const data = readFileSync(path);
      expect(data.length, `${part.file} size`).toBe(part.size);
      expect(createHash('sha256').update(data).digest('hex'), `${part.file} sha256`).toBe(
        part.sha256,
      );
    }
  });

  it('names the source it was built from', () => {
    const firmware = release();
    expect(firmware.commit).toMatch(/^[0-9a-f]{40}$/);
    expect(firmware.idf).toMatch(/^ESP-IDF v/);
    expect(Number.isNaN(Date.parse(firmware.built_at))).toBe(false);
  });
});
