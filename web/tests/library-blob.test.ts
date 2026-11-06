import { describe, expect, it } from 'vitest';
import { presenceFromLibraryBlob } from '../src/device/libraryBlob';

const HEADER = 64;
const NODE = 32;

interface NodeSpec {
  name: string;
  parent: number;
  kind: 0 | 1;
}

/** The smallest arena the device can send: header, nodes, edges, tracks, strings. */
function blob(nodes: NodeSpec[]): Uint8Array<ArrayBuffer> {
  const edgeCount = nodes.reduce((n, node) => n + (node.parent === 0xffffffff ? 0 : 1), 0);
  const trackCount = nodes.filter((node) => node.kind === 1).length;
  const names = new TextEncoder().encode(nodes.map((node) => `${node.name}\0`).join(''));
  const offsets: number[] = [];
  let at = 0;
  for (const node of nodes) {
    offsets.push(at);
    at += new TextEncoder().encode(node.name).length + 1;
  }
  const size = HEADER + nodes.length * NODE + edgeCount * 4 + trackCount * 4 + names.length;
  const bytes = new Uint8Array(size);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, 0x424c5458, true);
  view.setUint16(4, 1, true);
  view.setUint16(6, HEADER, true);
  view.setUint32(12, nodes.length, true);
  view.setUint32(16, edgeCount, true);
  view.setUint32(20, trackCount, true);
  view.setUint32(24, names.length, true);

  nodes.forEach((node, index) => {
    const at = HEADER + index * NODE;
    view.setUint32(at + 8, offsets[index], true);
    view.setUint32(at + 12, node.parent, true);
    view.setUint16(at + 22, new TextEncoder().encode(node.name).length, true);
    view.setUint8(at + 24, node.kind);
  });
  bytes.set(names, HEADER + nodes.length * NODE + edgeCount * 4 + trackCount * 4);
  return bytes;
}

describe('the device library blob', () => {
  it('builds the presence from every track on the card', () => {
    const presence = presenceFromLibraryBlob(
      blob([
        { name: 'Music', parent: 0xffffffff, kind: 0 },
        { name: 'Artist', parent: 0, kind: 0 },
        { name: 'Song.mp3', parent: 1, kind: 1 },
        { name: 'clip.wav', parent: 1, kind: 1 },
      ]),
    );

    expect([...presence.paths]).toEqual(['artist/song.mp3', 'artist/clip.wav']);
    expect(presence.tracks.map((track) => track.path)).toEqual(['Artist/Song.mp3']);
    expect(presence.unmeasurable.map((track) => track.path)).toEqual(['Artist/clip.wav']);
    expect(presence.complete).toBe(true);
  });

  it('is an empty allowlist for a card-less generation', () => {
    const presence = presenceFromLibraryBlob(
      blob([{ name: 'Music', parent: 0xffffffff, kind: 0 }]),
    );

    expect(presence.paths.size).toBe(0);
    expect(presence.tracks).toEqual([]);
    expect(presence.complete).toBe(true);
  });

  it('refuses a blob that is not one', () => {
    expect(() => presenceFromLibraryBlob(new Uint8Array(10))).toThrow(/header/);
    const bad = blob([{ name: 'Music', parent: 0xffffffff, kind: 0 }]);
    bad[0] = 0;
    expect(() => presenceFromLibraryBlob(bad)).toThrow(/library blob/);
  });
});
