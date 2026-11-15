import { describe, expect, it } from 'vitest';
import {
  encodeList,
  listFingerprint,
  listIdOf,
  listPath,
  ListFileError,
  readList,
  type ListFile,
} from './file';

/**
 * The playlist file, both ways.
 *
 * One list is one file on the card (docs/specs/playlist-format.md), written by this browser
 * when the card is in a computer and by the device when it is in the player (ADR 0094, ADR
 * 0095). The device's own reader is `components/portable/playlist_file`, so every rule here is
 * a rule that has a second implementation: what is pinned is the format, not this code.
 */

const LATE: ListFile = {
  id: '00000000000000a1',
  name: 'Late night 8A',
  createdAt: 1_700_000_000,
  rows: ['A/B/one.mp3', 'A/B/two.mp3'],
};

const FILE = '00000000000000a1.k7list';

describe('the file', () => {
  it('round-trips a list', () => {
    const text = encodeList(LATE);
    expect(text).toBe(
      'K7LIST 1\nid 00000000000000a1\nname Late night 8A\ncreated 1700000000\nrows 2\n' +
        'A/B/one.mp3\nA/B/two.mp3\n',
    );
    expect(readList(text, FILE)).toEqual(LATE);
  });

  it('carries a list with no rows', () => {
    const empty: ListFile = { ...LATE, id: '00000000000000a2', rows: [] };
    expect(readList(encodeList(empty), '00000000000000a2.k7list')).toEqual(empty);
  });

  it('accepts CRLF and a missing final newline', () => {
    const text = encodeList(LATE).replace(/\n/g, '\r\n').slice(0, -2);
    expect(readList(text, FILE)).toEqual(LATE);
  });

  it('writes the path below the card root, named by the id', () => {
    expect(listPath(LATE.id)).toBe('k7.playlists/00000000000000a1.k7list');
    expect(listIdOf(FILE)).toBe(LATE.id);
    expect(listIdOf(`${FILE}.tmp`)).toBeNull();
    expect(listIdOf('notes.txt')).toBeNull();
    expect(listIdOf('0000000000000000.k7list')).toBeNull();
  });
});

/**
 * A file the writer would refuse, so it can be built at all: the same format, typed out here
 * without going through `encodeList`'s validation.
 */
function raw(over: Partial<ListFile> = {}): string {
  const list = { ...LATE, ...over };
  const rows = list.rows.map((row) => `${row}\n`).join('');
  return `K7LIST 1\nid ${list.id}\nname ${list.name}\ncreated ${list.createdAt}\nrows ${list.rows.length}\n${rows}`;
}

describe('the refusals', () => {
  const cases: [string, string][] = [
    ['a wrong version', raw().replace('K7LIST 1', 'K7LIST 2')],
    ['a missing header line', raw().replace(/^id .*\n/m, '')],
    ['keys out of order', raw().replace(/^id (.*)\nname (.*)\n/m, 'name $2\nid $1\n')],
    ['an id of zero', raw({ id: '0000000000000000' })],
    ['a short id', raw({ id: 'a1' })],
    ['an empty name', raw({ name: '' })],
    ['a name with an edge space', raw({ name: ' Late' })],
    ['a name over 63 bytes', raw({ name: 'x'.repeat(64) })],
    ['a creation time that is not a number', raw().replace(/^created .*$/m, 'created soon')],
    ['a row count that disagrees', raw().replace('rows 2', 'rows 3')],
    ['a line after the rows', `${raw()}A/B/three.mp3\n`],
    ['an absolute row', raw({ rows: ['/A/B/one.mp3'] })],
    ['a row with ..', raw({ rows: ['A/../B/one.mp3'] })],
    ['an empty row', raw({ rows: [''] })],
  ];

  for (const [what, text] of cases) {
    it(`refuses ${what}`, () => {
      expect(() => readList(text, FILE)).toThrow(ListFileError);
    });
  }

  it('refuses an id that disagrees with the file name', () => {
    expect(() => readList(raw(), '00000000000000a2.k7list')).toThrow(/does not match/);
  });

  it('refuses to write a list the reader would not accept', () => {
    expect(() => encodeList({ ...LATE, name: '' })).toThrow(ListFileError);
    expect(() => encodeList({ ...LATE, rows: ['/A'] })).toThrow(ListFileError);
  });
});

describe('the fingerprint', () => {
  /* The device's value, computed by its own C implementation: a second implementation is the
   * only thing that makes this a contract rather than a self-check. */
  it('is the value the device computes', () => {
    expect(listFingerprint(LATE)).toBe('6142dbb394219d00');
    expect(
      listFingerprint({ name: 'Slow', createdAt: 1_700_000_100, rows: [] }),
    ).toBe('63ee51b66eeebbf1');
  });

  it('changes with the name, the time and the rows, and not with the line endings', () => {
    const other = { ...LATE, rows: ['A/B/one.mp3'] };
    expect(listFingerprint(other)).not.toBe(listFingerprint(LATE));
    expect(listFingerprint({ ...LATE, name: 'Late night 8B' })).not.toBe(listFingerprint(LATE));
    expect(listFingerprint({ ...LATE, createdAt: 1 })).not.toBe(listFingerprint(LATE));
    // Same content read back from a CRLF file: the hash is the content's, not the bytes'.
    const crlf = readList(encodeList(LATE).replace(/\n/g, '\r\n'), FILE);
    expect(listFingerprint(crlf)).toBe(listFingerprint(LATE));
  });
});
