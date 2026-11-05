import { describe, expect, it } from 'vitest';

import { readCardFile, type ReadableDirectory, type ReadableEntry } from '../src/card/read';
import { streamUrl } from '../src/ui/playback';

/**
 * Reading one track off the card in the computer, by the path the library names it by.
 *
 * The handle is structural, so a plain object tree is a card here: the walk is the thing
 * being tested, not the browser's picker.
 */

interface Node {
  kind: 'file' | 'directory';
  name: string;
  text?: string;
  children?: Node[];
}

function file(name: string, text = name): Node {
  return { kind: 'file', name, text };
}

function directory(name: string, children: Node[]): Node {
  return { kind: 'directory', name, children };
}

function handle(node: Node): ReadableDirectory {
  const entry = {
    kind: 'directory' as const,
    name: node.name,
    async *values(): AsyncIterable<ReadableEntry> {
      for (const child of node.children ?? []) {
        yield child.kind === 'file'
          ? { kind: 'file', name: child.name, getFile: async () => new File([child.text ?? ''], child.name) }
          : handle(child);
      }
    },
  };
  return entry;
}

const card = (musicName = 'music') =>
  handle(
    directory('SDCARD', [
      file('k7.db', 'image'),
      directory('System Volume Information', []),
      directory(musicName, [
        directory('Aphex Twin', [
          directory('Selected Ambient Works', [file('01 - Xtal.mp3', 'xtal'), file('02 - Tha.mp3', 'tha')]),
        ]),
        file('loose.mp3', 'loose'),
      ]),
    ]),
  );

describe('reading a card file', () => {
  it('walks music and the folders to the file, below the card root', async () => {
    const found = await readCardFile(card(), 'Aphex Twin/Selected Ambient Works/01 - Xtal.mp3');

    expect(found).not.toBeNull();
    expect(await found?.text()).toBe('xtal');
  });

  it('finds music in whatever case the card was formatted with', async () => {
    const found = await readCardFile(card('MUSIC'), 'loose.mp3');

    expect(await found?.text()).toBe('loose');
  });

  it('takes a file at the top of music, with no folders at all', async () => {
    const found = await readCardFile(card(), 'loose.mp3');

    expect(await found?.text()).toBe('loose');
  });

  it('is null when a folder or the file is gone, not an error', async () => {
    const root = card();

    expect(await readCardFile(root, 'Aphex Twin/Selected Ambient Works/03 - missing.mp3')).toBeNull();
    expect(await readCardFile(root, 'Nobody/Album/01.mp3')).toBeNull();
    expect(await readCardFile(root, '')).toBeNull();
  });

  it('is null for a card with no music directory', async () => {
    const root = handle(directory('SDCARD', [file('k7.db')]));

    expect(await readCardFile(root, 'loose.mp3')).toBeNull();
  });
});

describe('the stream address', () => {
  it('is the device’s own address, whichever page asks for it', () => {
    expect(streamUrl()).toBe('http://k7.local/stream');
  });
});
