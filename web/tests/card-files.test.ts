import { describe, expect, it } from 'vitest';

import {
  relocateInCard,
  removeFromCard,
  renameInCard,
  type CardDir,
  type CardFile,
} from '../src/card/files';

/**
 * The card's file manager: delete, rename and move are the card's own directory. Rename and
 * move are a copy followed by a delete on every browser, so the tests pin the bytes surviving
 * the copy and the original being gone — not just the path changing.
 */

interface DirNode {
  kind: 'directory';
  name: string;
  children: Map<string, Node>;
}
interface FileNode {
  kind: 'file';
  name: string;
  bytes: Uint8Array;
}
type Node = DirNode | FileNode;

const folder = (name: string): DirNode => ({ kind: 'directory', name, children: new Map() });
const track = (name: string, text: string): FileNode => ({
  kind: 'file',
  name,
  bytes: new TextEncoder().encode(text),
});

function cardFile(node: FileNode): CardFile {
  return {
    kind: 'file',
    name: node.name,
    async getFile() {
      return new File([node.bytes as unknown as BlobPart], node.name);
    },
    async createWritable() {
      const chunks: Uint8Array[] = [];
      return {
        async write(data: Uint8Array) {
          chunks.push(data.slice());
        },
        async close() {
          const total = chunks.reduce((n, chunk) => n + chunk.length, 0);
          const all = new Uint8Array(total);
          let at = 0;
          for (const chunk of chunks) {
            all.set(chunk, at);
            at += chunk.length;
          }
          node.bytes = all;
        },
      } as unknown as FileSystemWritableFileStream;
    },
  };
}

function cardDir(node: DirNode): CardDir {
  return {
    kind: 'directory',
    name: node.name,
    async getDirectoryHandle(name, options) {
      const child = node.children.get(name);
      if (child?.kind === 'directory') return cardDir(child);
      if (!options?.create) throw new Error(`no folder ${name}`);
      const made = folder(name);
      node.children.set(name, made);
      return cardDir(made);
    },
    async getFileHandle(name, options) {
      const child = node.children.get(name);
      if (child?.kind === 'file') return cardFile(child);
      if (!options?.create) throw new Error(`no file ${name}`);
      const made = track(name, '');
      node.children.set(name, made);
      return cardFile(made);
    },
    async removeEntry(name) {
      node.children.delete(name);
    },
    async *values() {
      for (const child of node.children.values()) {
        yield child.kind === 'directory' ? cardDir(child) : cardFile(child);
      }
    },
  };
}

/** `card/music/Artist/a.mp3`, the shape every test starts from. */
function card(): { root: DirNode; artist: DirNode } {
  const root = folder('card');
  const music = folder('music');
  const artist = folder('Artist');
  artist.children.set('a.mp3', track('a.mp3', 'A'));
  music.children.set('Artist', artist);
  root.children.set('music', music);
  return { root, artist };
}

describe('card files', () => {
  it('deletes a file from the card', async () => {
    const { root, artist } = card();
    await removeFromCard(cardDir(root), 'Artist/a.mp3');
    expect(artist.children.has('a.mp3')).toBe(false);
  });

  it('deletes a folder and everything under it', async () => {
    const { root } = card();
    await removeFromCard(cardDir(root), 'Artist');
    expect((root.children.get('music') as DirNode).children.has('Artist')).toBe(false);
  });

  it('renames a file, keeping its bytes', async () => {
    const { root, artist } = card();
    const next = await renameInCard(cardDir(root), 'Artist/a.mp3', 'b.mp3');
    expect(next).toBe('Artist/b.mp3');
    expect(artist.children.has('a.mp3')).toBe(false);
    const moved = artist.children.get('b.mp3') as FileNode;
    expect(new TextDecoder().decode(moved.bytes)).toBe('A');
  });

  it('moves a file into another folder', async () => {
    const { root, artist } = card();
    const other = folder('Other');
    (root.children.get('music') as DirNode).children.set('Other', other);
    const next = await relocateInCard(cardDir(root), 'Artist/a.mp3', 'Other');
    expect(next).toBe('Other/a.mp3');
    expect(artist.children.has('a.mp3')).toBe(false);
    expect(new TextDecoder().decode((other.children.get('a.mp3') as FileNode).bytes)).toBe('A');
  });

  it('says nothing moved when the name and folder are unchanged', async () => {
    const { root } = card();
    expect(await relocateInCard(cardDir(root), 'Artist/a.mp3', 'Artist')).toBeNull();
  });
});
