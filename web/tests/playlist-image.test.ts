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
  // 'One', 'Alpha', 'One', 'house', each NUL-terminated: the row strings the image carries.
  const rowPool = 4 + 6 + 4 + 6;

  it('keeps the rows and their strings byte for byte and adds a v2 list area', () => {
    const v1 = buildSnapshot(input);
    const v2 = patchListArea(v1, [list]);

    expect(v2.length).toBeGreaterThan(v1.length);
    expect(new TextDecoder().decode(v2.subarray(0, 8))).toBe('K7SNAP\0\0');
    expect(new DataView(v2.buffer).getUint16(8, true)).toBe(2);
    expect(new DataView(v2.buffer).getUint16(10, true)).toBe(80);
    expect(u32(v2, 12)).toBe(v2.length);
    expect(u32(v2, 16)).toBe(1);
    expect(u32(v2, 32)).toBe(rowPool + 'Late night'.length + 1 + 'Alpha/One/one.flac'.length + 1);

    // The row is the same 32 bytes, and its strings are the same bytes at the same offset.
    expect(Array.from(v2.subarray(80, 112))).toEqual(Array.from(v1.subarray(64, 96)));
    const rowOffset = u32(v2, 80 + 12);
    expect(nul(v2, 80 + 32 + rowOffset)).toBe('One');

    // The record's name and its one path resolve inside the pool.
    const lists = 80 + 32 + u32(v2, 32);
    expect(new DataView(v2.buffer).getUint16(64, true)).toBe(1);
    expect(new DataView(v2.buffer).getBigUint64(lists, true)).toBe(0xaaaa000000000001n);
    const nameOffset = u32(v2, lists + 8);
    expect(nul(v2, 80 + 32 + nameOffset)).toBe('Late night');
    expect(u32(v2, lists + 16)).toBe(1);
    const listRows = lists + 32;
    expect(nul(v2, 80 + 32 + u32(v2, listRows))).toBe('Alpha/One/one.flac');
    expect(crc32(v2.subarray(80))).toBe(u32(v2, 36));
  });

  it('replaces the lists instead of piling them up', () => {
    const first = patchListArea(buildSnapshot(input), [list]);
    const second = patchListArea(first, [
      { ...list, id: 'bbbb000000000002', name: 'Short', rows: [] },
    ]);
    const third = patchListArea(second, []);

    expect(new DataView(second.buffer).getUint16(8, true)).toBe(2);
    // The row strings, then only the new list's name: the old list's strings are gone.
    expect(u32(second, 32)).toBe(rowPool + 'Short'.length + 1);

    // A patch back to no lists is a version 1 image with only the row strings again.
    expect(new DataView(third.buffer).getUint16(8, true)).toBe(1);
    expect(third.length).toBe(64 + 32 + u32(third, 32));
    expect(crc32(third.subarray(64))).toBe(u32(third, 36));
  });

  it('refuses bytes that are not an image', () => {
    expect(() => patchListArea(new Uint8Array(4), [])).toThrow(ImageError);
    const notMagic = buildSnapshot(input);
    notMagic[0] = 0;
    expect(() => patchListArea(notMagic, [])).toThrow(/not a Snapshot/);
    const cut = buildSnapshot(input).slice(0, 70);
    expect(() => patchListArea(cut, [])).toThrow(ImageError);
  });
});
