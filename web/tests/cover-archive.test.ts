import { describe, expect, it } from 'vitest';
import {
  buildCoverArchive,
  parseCoverArchive,
  readCoverArchive,
  type ArchiveCover,
} from '../src/storage/coverArchive';

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

  it('carries which album wears which picture', () => {
    const archive = buildCoverArchive(
      [cover('b', [2, 2]), cover('a', [1, 1, 1])],
      [
        { path: 'Sting/Ten Summoner\'s Tales', hash: hash('a') },
        { path: 'Sting/Nothing Like the Sun', hash: hash('b') },
        { path: 'Gone/Not in the archive', hash: hash('f') },
      ],
    );
    const { covers, albums } = readCoverArchive(archive);
    expect(covers).toHaveLength(2);
    expect([...albums.entries()]).toEqual([
      ["Sting/Nothing Like the Sun", hash('b')],
      ["Sting/Ten Summoner's Tales", hash('a')],
    ]);
  });

  it('writes the same bytes as before when there is no index', () => {
    const one = buildCoverArchive([cover('b', [2]), cover('a', [1])]);
    const two = buildCoverArchive([cover('a', [1]), cover('b', [2])], []);
    expect([...one]).toEqual([...two]);
    expect(readCoverArchive(one).albums.size).toBe(0);
  });

  it('refuses an index that names a cover it does not hold', () => {
    const archive = buildCoverArchive([cover('a', [1])], [{ path: 'x', hash: hash('a') }]);
    const broken = Uint8Array.from(archive);
    /* The index's hash is the last 32-byte run before its path: corrupt its first byte. */
    const hashAt = archive.length - (2 + 1) - 32;
    broken[hashAt] = 0x00;
    broken[hashAt + 1] = 0x00;
    expect(() => readCoverArchive(broken)).toThrow();
  });
});
