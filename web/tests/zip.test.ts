import { describe, expect, it } from 'vitest';
import { crc32, zipStore } from '../src/lib/zip';

const bytes = (text: string) => new TextEncoder().encode(text);

/** The smallest reader: walk the central directory and check what it says. */
function readCentral(archive: Uint8Array) {
  const view = new DataView(archive.buffer, archive.byteOffset, archive.byteLength);
  const end = archive.length - 22;
  expect(view.getUint32(end, true)).toBe(0x06054b50);
  const count = view.getUint16(end + 8, true);
  let at = view.getUint32(end + 16, true);
  const entries = [];
  for (let i = 0; i < count; i += 1) {
    expect(view.getUint32(at, true)).toBe(0x02014b50);
    const nameLength = view.getUint16(at + 28, true);
    const name = new TextDecoder().decode(archive.subarray(at + 46, at + 46 + nameLength));
    entries.push({
      name,
      crc: view.getUint32(at + 16, true),
      size: view.getUint32(at + 24, true),
      offset: view.getUint32(at + 42, true),
    });
    at += 46 + nameLength;
  }
  return entries;
}

describe('the archive the covers are delivered in', () => {
  it('is a store-only zip whose names and checksums survive the round trip', () => {
    const archive = zipStore([
      { path: 'Artist/Album/cover.jpg', bytes: bytes('front') },
      { path: 'Artist/Album/back.png', bytes: bytes('back') },
    ]);

    const entries = readCentral(archive);
    expect(entries.map((entry) => entry.name)).toEqual([
      'Artist/Album/cover.jpg',
      'Artist/Album/back.png',
    ]);
    expect(entries[0].crc).toBe(crc32(bytes('front')));
    expect(entries[1].size).toBe(4);
  });

  it('points each entry at its own local header', () => {
    const archive = zipStore([
      { path: 'a.jpg', bytes: bytes('one') },
      { path: 'b.jpg', bytes: bytes('two') },
    ]);
    const view = new DataView(archive.buffer, archive.byteOffset, archive.byteLength);
    for (const entry of readCentral(archive)) {
      expect(view.getUint32(entry.offset, true)).toBe(0x04034b50);
      const nameLength = view.getUint16(entry.offset + 26, true);
      const name = new TextDecoder().decode(
        archive.subarray(entry.offset + 30, entry.offset + 30 + nameLength),
      );
      expect(name).toBe(entry.name);
    }
  });

  it('uses the standard checksum', () => {
    expect(crc32(bytes('hello'))).toBe(0x3610a686);
  });
});
