import { describe, expect, it } from 'vitest';
import { buildSnapshot, type SnapshotInput, type SnapshotPlaylist } from '../src/domain/snapshot';
import { ImageError, patchListArea } from '../src/playlists/image';
import { sampleAnalysis } from './sampleAnalysis';

const input: SnapshotInput = {
  generationSeconds: 1767225600,
  analyserVersion: 300,
  appSchema: 2,
  modelSet: 0x1234n,
  tracks: [
    {
      path: 'CARD/music/Alpha/One/one.flac',
      size: 1234,
      title: 'One',
      artist: 'Alpha',
      album: 'One',
      genre: 'house',
      analysis: sampleAnalysis,
    },
  ],
};

const list: SnapshotPlaylist = {
  id: 'aaaa000000000001',
  name: 'Late night',
  createdAt: 1767225600,
  rows: ['Alpha/One/one.flac'],
};

function u16(bytes: Uint8Array, at: number): number {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint16(at, true);
}

function u32(bytes: Uint8Array, at: number): number {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(at, true);
}

/** CRC-32/IEEE, written out here so the check does not share the producer's table. */
function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}

const nul = (bytes: Uint8Array, at: number): string => {
  let end = at;
  while (end < bytes.length && bytes[end] !== 0) end += 1;
  return new TextDecoder().decode(bytes.subarray(at, end));
};

describe('giving an image new Playlists', () => {
  // 'One', 'Alpha', 'One', 'house', each NUL-terminated; the album shares the
  // title's offset, so the interned row pool is 4 + 6 + 6 = 16 bytes.
  const rowPool = 4 + 6 + 6;

  it('keeps the rows and their strings byte for byte and rebuilds only the list area', () => {
    const original = buildSnapshot(input);
    const patched = patchListArea(original, [list]);

    expect(original.length).toBe(80 + 32 + rowPool);
    expect(patched.length).toBe(80 + 32 + rowPool + 11 + 19 + 32 + 4);
    expect(new TextDecoder().decode(patched.subarray(0, 8))).toBe('K7SNAP\0\0');
    expect(u16(patched, 8)).toBe(3);
    expect(u16(patched, 10)).toBe(80);
    expect(u32(patched, 12)).toBe(patched.length);
    expect(u32(patched, 16)).toBe(1);
    expect(u32(patched, 20)).toBe(32);
    expect(u32(patched, 32)).toBe(rowPool + 'Late night'.length + 1 + 'Alpha/One/one.flac'.length + 1);
    expect(u32(patched, 76)).toBe(rowPool);

    // The measurement area — the rows and the whole row pool — is byte-identical.
    expect(Array.from(patched.subarray(80, 80 + 32 + rowPool))).toEqual(
      Array.from(original.subarray(80, 80 + 32 + rowPool)),
    );
    expect(Array.from(patched.subarray(80, 112))).toEqual(Array.from(original.subarray(80, 112)));

    // The row's four offsets still resolve in the copied pool, including the shared album.
    const poolStart = 80 + 32;
    const titleOffset = patched[92] | (patched[93] << 8) | (patched[94] << 16);
    const albumOffset = patched[98] | (patched[99] << 8) | (patched[100] << 16);
    expect(titleOffset).toBe(0);
    expect(albumOffset).toBe(0);
    expect(nul(patched, poolStart + titleOffset)).toBe('One');
    expect(nul(patched, poolStart + albumOffset)).toBe('One');
    expect(nul(patched, poolStart + 4)).toBe('Alpha');
    expect(nul(patched, poolStart + 10)).toBe('house');

    // The rebuilt list area sits after the row pool and resolves through it.
    const lists = poolStart + u32(patched, 32);
    expect(u16(patched, 64)).toBe(1);
    expect(u16(patched, 66)).toBe(32);
    expect(u16(patched, 68)).toBe(4);
    expect(u16(patched, 70)).toBe(0);
    expect(u32(patched, 72)).toBe(1);
    expect(new DataView(patched.buffer).getBigUint64(lists, true)).toBe(0xaaaa000000000001n);
    const nameOffset = u32(patched, lists + 8);
    expect(nameOffset).toBe(rowPool);
    expect(nul(patched, poolStart + nameOffset)).toBe('Late night');
    expect(u32(patched, lists + 16)).toBe(1);
    const listRows = lists + 32;
    expect(nul(patched, poolStart + u32(patched, listRows))).toBe('Alpha/One/one.flac');
    expect(crc32(patched.subarray(80))).toBe(u32(patched, 36));
  });

  it('replaces the lists instead of piling them up', () => {
    const original = buildSnapshot(input);
    const first = patchListArea(original, [list]);
    const second = patchListArea(first, [
      { ...list, id: 'bbbb000000000002', name: 'Short', rows: [] },
    ]);
    const third = patchListArea(second, []);

    expect(u16(second, 8)).toBe(3);
    // The row strings, then only the new list's name: the old list's strings are gone.
    expect(u32(second, 32)).toBe(rowPool + 'Short'.length + 1);
    expect(u32(second, 76)).toBe(rowPool);

    // A patch back to no lists is still version 3, with an empty list area.
    expect(u16(third, 8)).toBe(3);
    expect(u16(third, 10)).toBe(80);
    expect(u16(third, 64)).toBe(0);
    expect(u32(third, 72)).toBe(0);
    expect(u32(third, 76)).toBe(rowPool);
    expect(u32(third, 32)).toBe(rowPool);
    expect(third.length).toBe(80 + 32 + rowPool);
    expect(Array.from(third)).toEqual(Array.from(original));
  });

  it('refuses bytes that are not a version 3 image', () => {
    expect(() => patchListArea(new Uint8Array(4), [])).toThrow(ImageError);

    const notMagic = buildSnapshot(input);
    notMagic[0] = 0;
    expect(() => patchListArea(notMagic, [])).toThrow(/not a Snapshot/);

    const cut = buildSnapshot(input).slice(0, 70);
    expect(() => patchListArea(cut, [])).toThrow(ImageError);

    const wrongVersion = buildSnapshot(input);
    new DataView(wrongVersion.buffer).setUint16(8, 2, true);
    expect(() => patchListArea(wrongVersion, [])).toThrow(/unknown format version/);

    const wrongHeader = buildSnapshot(input);
    new DataView(wrongHeader.buffer).setUint16(10, 64, true);
    expect(() => patchListArea(wrongHeader, [])).toThrow(ImageError);

    const wrongTotal = buildSnapshot(input).slice(0, 100);
    expect(() => patchListArea(wrongTotal, [])).toThrow(ImageError);

    const badPool = buildSnapshot(input);
    new DataView(badPool.buffer).setUint32(76, u32(badPool, 32) + 1, true);
    expect(() => patchListArea(badPool, [])).toThrow(/outside the pool/);
  });
});
