import { describe, expect, it } from 'vitest';
import { buildCoverArchive, parseCoverArchive, type ArchiveCover } from '../src/storage/coverArchive';

const hash = (byte: string) => byte.repeat(64).slice(0, 64);

function cover(byte: string, body: number[], format = 'image/avif'): ArchiveCover {
  return { hash: hash(byte), bytes: new Uint8Array(body), format, width: 422, height: 300 };
}

describe('k7.covers', () => {
  it('round-trips the records in hash order', () => {
    const archive = buildCoverArchive([cover('b', [2, 2]), cover('a', [1, 1, 1])]);
    const parsed = parseCoverArchive(archive);
    expect(parsed.map((entry) => entry.hash)).toEqual([hash('a'), hash('b')]);
    expect([...parsed[0].bytes]).toEqual([1, 1, 1]);
    expect(parsed[0].format).toBe('image/avif');
    expect(parsed[0].width).toBe(422);
  });

  it('carries a fallback source format', () => {
    const archive = buildCoverArchive([cover('c', [7], 'image/jpeg')]);
    expect(parseCoverArchive(archive)[0].format).toBe('image/jpeg');
  });

  it('is deterministic', () => {
    const one = buildCoverArchive([cover('b', [2]), cover('a', [1])]);
    const two = buildCoverArchive([cover('a', [1]), cover('b', [2])]);
    expect([...one]).toEqual([...two]);
  });

  it('refuses a corrupt payload, a short file and a bad magic', () => {
    const archive = buildCoverArchive([cover('a', [1, 2, 3])]);
    const corrupt = archive.slice();
    corrupt[corrupt.length - 1] ^= 0xff;
    expect(() => parseCoverArchive(corrupt)).toThrow(/checksum/);
    expect(() => parseCoverArchive(archive.slice(0, 10))).toThrow(/header/);
    const magic = archive.slice();
    magic[0] = 0;
    expect(() => parseCoverArchive(magic)).toThrow(/not a cover archive/);
  });

  it('accepts an empty archive', () => {
    expect(parseCoverArchive(buildCoverArchive([]))).toEqual([]);
  });
});
