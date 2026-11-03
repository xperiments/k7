import { crc32 } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import type { Analysis } from '../src/domain/analysis';
import {
  buildSnapshot,
  fnv1a64,
  playlistId,
  snapshotPath,
  type SnapshotInput,
  type SnapshotPlaylist,
  type SnapshotTrack,
} from '../src/domain/snapshot';
import { sampleAnalysis } from './sampleAnalysis';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

const HEADER_SIZE = 64;
const HEADER_SIZE_V2 = 80;
const ROW_SIZE = 32;
const LIST_SIZE = 32;
const LIST_ROW_SIZE = 4;

interface AnalysisPatch {
  tempo?: Partial<Analysis['tempo']>;
  key?: Partial<Analysis['key']>;
  mood?: Partial<Analysis['mood']>;
  energy?: number | null;
  energySource?: Analysis['energySource'];
}

function analysis(patch: AnalysisPatch = {}): Analysis {
  return {
    ...sampleAnalysis,
    tempo: { ...sampleAnalysis.tempo, ...patch.tempo },
    key: { ...sampleAnalysis.key, ...patch.key },
    mood: { ...sampleAnalysis.mood, ...patch.mood },
    energy: patch.energy === undefined ? sampleAnalysis.energy : patch.energy,
    energySource:
      patch.energySource === undefined ? sampleAnalysis.energySource : patch.energySource,
  };
}

function track(overrides: Partial<SnapshotTrack> & { path: string }): SnapshotTrack {
  return {
    size: 1_000,
    title: 'Title',
    artist: 'Artist',
    album: 'Album',
    genre: 'Genre',
    analysis: sampleAnalysis,
    ...overrides,
  };
}

function input(tracks: SnapshotTrack[], overrides: Partial<SnapshotInput> = {}): SnapshotInput {
  return {
    tracks,
    generationSeconds: 1_700_000_000,
    analyserVersion: 10_400,
    appSchema: 1,
    modelSet: 0x0123456789abcdefn,
    ...overrides,
  };
}

/**
 * The fixture every byte assertion uses. Its values are chosen so every field
 * has a distinct, unambiguous encoding: 124.54 BPM rounds to 1245, 0.784
 * energy to 78, and the string pool is 28 bytes.
 */
function canonicalInput(): SnapshotInput {
  return input([
    {
      path: 'CARD/music/Artist/Album/01 - Track.mp3',
      size: 4_000_000,
      title: 'Track One',
      artist: 'Artist',
      album: 'Album',
      genre: 'Rock',
      analysis: analysis({
        tempo: { bpm: 124.54 },
        key: { camelot: '8A' },
        mood: { valence: 0.4, arousal: 0.6 },
        energy: 0.784,
      }),
    },
  ]);
}

function rowOffset(index: number, headerSize = HEADER_SIZE): number {
  return headerSize + index * ROW_SIZE;
}

function viewOf(image: Uint8Array): DataView {
  return new DataView(image.buffer, image.byteOffset, image.byteLength);
}

function readString(image: Uint8Array, offset: number): { value: string; next: number } {
  let end = offset;
  while (image[end] !== 0) end += 1;
  return { value: decoder.decode(image.subarray(offset, end)), next: end + 1 };
}

/** The four strings a row points at, read through the header's pool fields. */
function rowStrings(image: Uint8Array, index: number, headerSize = HEADER_SIZE): string[] {
  const view = viewOf(image);
  const poolStart = headerSize + view.getUint32(16, true) * ROW_SIZE;
  let offset = poolStart + view.getUint32(rowOffset(index) + 12, true);

  const values: string[] = [];
  for (let part = 0; part < 4; part += 1) {
    const { value, next } = readString(image, offset);
    values.push(value);
    offset = next;
  }
  return values;
}

describe('snapshot paths', () => {
  it('drops the volume name and the music directory', () => {
    expect(snapshotPath('MYCARD/music/Artist/Album/01 - Track.mp3')).toBe(
      'Artist/Album/01 - Track.mp3',
    );
    expect(snapshotPath('music/Artist/Album/x.mp3')).toBe('Artist/Album/x.mp3');
    expect(snapshotPath('CARD/music/x.mp3')).toBe('x.mp3');
    expect(snapshotPath('CARD/music/Artist/music/x.mp3')).toBe('Artist/music/x.mp3');
  });

  it('returns null outside the music directory', () => {
    expect(snapshotPath('MYCARD/Artist/Album/x.mp3')).toBeNull();
    expect(snapshotPath('MYCARD/mymusic/Album/x.mp3')).toBeNull();
    expect(snapshotPath('MYCARD/Music/Album/x.mp3')).toBeNull();
    expect(snapshotPath('MYCARD/music')).toBeNull();
    expect(snapshotPath('MYCARD/music/')).toBeNull();
  });

  it('only accepts music at the root or as the card\'s child', () => {
    // A `music` directory inside the library is not the Music directory: the card
    // root is the selection, so the segment is either first or second and no deeper.
    expect(snapshotPath('MYCARD/Album/music/x.mp3')).toBeNull();
    expect(snapshotPath('MYCARD/Album/music/Sub/x.mp3')).toBeNull();
  });

  it('does not normalise case or Unicode', () => {
    expect(snapshotPath('CARD/music/Álbum/01.mp3')).toBe('Álbum/01.mp3');
    expect(snapshotPath('CARD/music/album/01.mp3')).not.toBe(
      snapshotPath('CARD/music/Album/01.mp3'),
    );
  });
});

describe('FNV-1a 64', () => {
  it('matches the published vectors', () => {
    expect(fnv1a64(new Uint8Array())).toBe(0xcbf29ce484222325n);
    expect(fnv1a64(encoder.encode('a'))).toBe(0xaf63dc4c8601ec8cn);
    expect(fnv1a64(encoder.encode('foobar'))).toBe(0x85944171f73967e8n);
  });
});

describe('snapshot image', () => {
  it('writes the header and one row', () => {
    const image = buildSnapshot(canonicalInput());
    const view = viewOf(image);

    expect(decoder.decode(image.subarray(0, 8))).toBe('K7SNAP\0\0');
    expect(view.getUint16(8, true)).toBe(1);
    expect(view.getUint16(10, true)).toBe(64);
    expect(image.byteLength).toBe(124);
    expect(view.getUint32(12, true)).toBe(image.byteLength);
    expect(view.getUint32(16, true)).toBe(1);
    expect(view.getUint32(20, true)).toBe(32);
    expect(view.getBigUint64(24, true)).toBe(1_700_000_000n);
    expect(view.getUint32(32, true)).toBe(28);
    expect(view.getUint32(40, true)).toBe(10_400);
    expect(view.getUint32(44, true)).toBe(1);
    expect(view.getBigUint64(48, true)).toBe(0x0123456789abcdefn);
    expect(view.getBigUint64(56, true)).toBe(0n);

    const row = rowOffset(0);
    expect(view.getBigUint64(row, true)).toBe(0x1b664b4fc563e884n);
    expect(view.getBigUint64(row, true)).toBe(
      fnv1a64(encoder.encode('Artist/Album/01 - Track.mp3')),
    );
    expect(view.getUint32(row + 8, true)).toBe(4_000_000);
    expect(view.getUint32(row + 12, true)).toBe(0);
    expect(view.getUint16(row + 16, true)).toBe(1245);
    expect(image[row + 18]).toBe(8);
    expect(image[row + 23]).toBe(0x08);
    expect(image[row + 24]).toBe(90);
    expect(image[row + 25]).toBe(0x10);
    expect(Array.from(image.subarray(row + 26, row + 32))).toEqual([0, 0, 0, 0, 0, 0]);
    expect(rowStrings(image, 0)).toEqual(['Track One', 'Artist', 'Album', 'Rock']);
  });

  it('rounds BPM and energy half away from zero', () => {
    const image = buildSnapshot(
      input([
        track({
          path: 'CARD/music/rounding.mp3',
          analysis: analysis({ tempo: { bpm: 124.54 }, energy: 0.784 }),
        }),
      ]),
    );

    expect(viewOf(image).getUint16(rowOffset(0) + 16, true)).toBe(1245);
    expect(image[rowOffset(0) + 19]).toBe(78);
  });

  it('writes unknown marks for a tagged mood with no model', () => {
    const tagged = analysis({
      tempo: { bpm: 0, rawBpm: 0, confidence: null, engine: null, source: 'metadata' },
      key: { tonic: '', mode: 'major', camelot: '', strength: null, source: 'metadata' },
      mood: { tags: null, label: 'Happy', source: 'metadata', valence: null, arousal: null },
      energy: null,
      energySource: null,
    });
    const image = buildSnapshot(input([track({ path: 'CARD/music/tagged.mp3', analysis: tagged })]));
    const row = rowOffset(0);

    expect(viewOf(image).getUint16(row + 16, true)).toBe(0);
    expect(image[row + 18]).toBe(0);
    expect(image[row + 19]).toBe(0xff);
    expect(image[row + 20]).toBe(0xff);
    expect(image[row + 21]).toBe(0xff);
    expect(image[row + 22]).toBe(0xff);
    expect(image[row + 23]).toBe(0);
    expect(image[row + 24]).toBe(0xff);
    expect(image[row + 25]).toBe(0x07);
    expect(image[row + 25] & 0x10).toBe(0);
  });

  it('sets mood bits at 0.5 and leaves danceability out', () => {
    const tags = {
      happy: 0.5,
      sad: 0.5,
      aggressive: 0.49,
      relaxed: 0.5,
      party: 0.5,
      danceable: 0.9,
    };
    const image = buildSnapshot(
      input([track({ path: 'CARD/music/mood.mp3', analysis: analysis({ mood: { tags } }) })]),
    );
    const row = rowOffset(0);

    expect(image[row + 23]).toBe(0x01 | 0x02 | 0x08 | 0x10);
    expect(image[row + 20]).toBe(90);
  });

  it('records where each measurement came from', () => {
    const measured = analysis({
      tempo: { source: 'metadata', escalated: true, confidence: null },
      key: { source: 'metadata' },
      mood: { source: 'measured', valence: 0.4, arousal: 0.6 },
      energy: 70,
      energySource: 'metadata',
    });
    const image = buildSnapshot(
      input([track({ path: 'CARD/music/sources.mp3', analysis: measured })]),
    );
    const row = rowOffset(0);

    expect(image[row + 19]).toBe(70);
    expect(image[row + 25]).toBe(0x01 | 0x02 | 0x08 | 0x10 | 0x20);
  });

  it('maps Camelot codes to the format values', () => {
    const cases: Array<[string, number]> = [
      ['1A', 1],
      ['12A', 12],
      ['1B', 13],
      ['12B', 24],
      ['13A', 0],
      ['', 0],
    ];

    for (const [code, expected] of cases) {
      const image = buildSnapshot(
        input([
          track({
            path: `CARD/music/key-${code || 'none'}.mp3`,
            analysis: analysis({ key: { camelot: code } }),
          }),
        ]),
      );
      expect(image[rowOffset(0) + 18]).toBe(expected);
    }
  });

  it('sorts rows by fingerprint regardless of input order', () => {
    const one = track({ path: 'CARD/music/A/one.mp3', title: 'One' });
    const two = track({ path: 'CARD/music/B/two.mp3', title: 'Two' });
    const forward = buildSnapshot(input([one, two]));
    const reverse = buildSnapshot(input([two, one]));

    expect(Array.from(forward)).toEqual(Array.from(reverse));

    const fingerprints = [
      fnv1a64(encoder.encode('A/one.mp3')),
      fnv1a64(encoder.encode('B/two.mp3')),
    ].sort((left, right) => (left < right ? -1 : left > right ? 1 : 0));

    const view = viewOf(forward);
    expect(view.getBigUint64(rowOffset(0), true)).toBe(fingerprints[0]);
    expect(view.getBigUint64(rowOffset(1), true)).toBe(fingerprints[1]);
  });

  it('stamps the header with a CRC over the rows and pool', () => {
    const image = buildSnapshot(canonicalInput());

    // Python: zlib.crc32(row + pool) over the fixture assembled from the
    // format, independently of buildSnapshot. The post-header bytes are
    // 32 + 28.
    expect(crc32(image.subarray(HEADER_SIZE))).toBe(0x17ef7ef3);
    expect(viewOf(image).getUint32(36, true)).toBe(0x17ef7ef3);
  });

  it('refuses two tracks with the same relative path', () => {
    const duplicate = track({ path: 'CARD/music/Artist/Album/x.mp3' });

    expect(() => buildSnapshot(input([duplicate, { ...duplicate, title: 'Other' }]))).toThrow(
      /duplicate fingerprint/,
    );
  });

  it('refuses paths outside the music directory', () => {
    expect(() =>
      buildSnapshot(input([track({ path: 'CARD/Artist/Album/x.mp3' })])),
    ).toThrow(/music directory/);
  });

  it('refuses a BPM that is not folded into 70-180', () => {
    expect(() =>
      buildSnapshot(
        input([track({ path: 'CARD/music/x.mp3', analysis: analysis({ tempo: { bpm: 200 } }) })]),
      ),
    ).toThrow(/70-180/);
    expect(() =>
      buildSnapshot(
        input([track({ path: 'CARD/music/x.mp3', analysis: analysis({ tempo: { bpm: 69 } }) })]),
      ),
    ).toThrow(/70-180/);
  });

  it('refuses an image larger than 2 MB', () => {
    const oversized = track({
      path: 'CARD/music/huge.mp3',
      title: 'x'.repeat(2 * 1024 * 1024),
    });

    expect(() => buildSnapshot(input([oversized]))).toThrow(/larger than 2097152/);
  });
});

/* ------------------------------------------------------------------ *
 * Format version 2: Playlists (§10 of docs/specs/snapshot-format.md)
 * ------------------------------------------------------------------ */

interface V2Areas {
  headerSize: number;
  poolStart: number;
  poolSize: number;
  listCount: number;
  listRows: number;
  listsStart: number;
  listRowsStart: number;
}

/** The five regions, read back through the header exactly as the C reader would. */
function v2Areas(image: Uint8Array): V2Areas {
  const view = viewOf(image);
  const headerSize = view.getUint16(10, true);
  const rowCount = view.getUint32(16, true);
  const poolSize = view.getUint32(32, true);
  const listCount = view.getUint16(64, true);
  const listRows = view.getUint32(72, true);
  const poolStart = headerSize + rowCount * ROW_SIZE;
  const listsStart = poolStart + poolSize;
  return {
    headerSize,
    poolStart,
    poolSize,
    listCount,
    listRows,
    listsStart,
    listRowsStart: listsStart + listCount * LIST_SIZE,
  };
}

function listRecord(image: Uint8Array, index: number) {
  const view = viewOf(image);
  const at = v2Areas(image).listsStart + index * LIST_SIZE;
  return {
    id: view.getBigUint64(at, true),
    nameOffset: view.getUint32(at + 8, true),
    rowStart: view.getUint32(at + 12, true),
    rowCount: view.getUint32(at + 16, true),
    created: view.getBigUint64(at + 20, true),
    reserved: Array.from(image.subarray(at + 28, at + 32)),
  };
}

/** One 4-byte list row: a path offset into the pool, and nothing else. */
function listRow(image: Uint8Array, index: number) {
  const view = viewOf(image);
  const at = v2Areas(image).listRowsStart + index * LIST_ROW_SIZE;
  return { pathOffset: view.getUint32(at, true), bytes: Array.from(image.subarray(at, at + 4)) };
}

/** A pool string at an offset relative to the pool, as both offsets in a record are. */
function poolString(image: Uint8Array, relativeOffset: number): string {
  return readString(image, v2Areas(image).poolStart + relativeOffset).value;
}

function playlist(
  overrides: Pick<SnapshotPlaylist, 'id' | 'name' | 'rows'> &
    Partial<Pick<SnapshotPlaylist, 'createdAt'>>,
): SnapshotPlaylist {
  return { createdAt: 1_710_000_000, ...overrides };
}

describe('snapshot images with Playlists', () => {
  /**
   * The v2 fixture. Its numbers are worked out from the format rather than from the
   * writer: the canonical track's four strings are 28 bytes, the name is 12 with its
   * NUL, the two paths are 28 and 29, so the pool is 97 and the image is
   * 80 + 32 + 97 + 32 + 2*4 = 249.
   */
  function v2Input(): [SnapshotInput, SnapshotPlaylist[]] {
    return [
      canonicalInput(),
      [
        playlist({
          id: 0x0123456789abcdefn,
          name: 'Night drive',
          rows: ['Artist/Album/01 - Track.mp3', 'Artist/Album/02 - Other.flac'],
        }),
      ],
    ];
  }

  it('writes the version 2 header and the list counts at their offsets', () => {
    const [input, lists] = v2Input();
    const image = buildSnapshot(input, lists);
    const view = viewOf(image);

    expect(decoder.decode(image.subarray(0, 8))).toBe('K7SNAP\0\0');
    expect(view.getUint16(8, true)).toBe(2);
    expect(view.getUint16(10, true)).toBe(80);
    expect(image.byteLength).toBe(249);
    expect(view.getUint32(12, true)).toBe(249);
    expect(view.getUint32(16, true)).toBe(1);
    expect(view.getUint32(20, true)).toBe(32);
    expect(view.getUint32(32, true)).toBe(97);
    expect(view.getBigUint64(56, true)).toBe(0n);

    // §3.1, bytes 64..80.
    expect(view.getUint16(64, true)).toBe(1);
    expect(view.getUint16(66, true)).toBe(32);
    expect(view.getUint16(68, true)).toBe(4);
    expect(view.getUint16(70, true)).toBe(0);
    expect(view.getUint32(72, true)).toBe(2);
    expect(view.getUint32(76, true)).toBe(0);

    // The CRC starts at the end of the header, which is now 80 bytes.
    expect(view.getUint32(36, true)).toBe(crc32(image.subarray(HEADER_SIZE_V2)));
    expect(view.getUint32(36, true)).not.toBe(crc32(image.subarray(HEADER_SIZE)));
  });

  it('keeps the version 1 header bytes 0..56 apart from the version and size', () => {
    const [input] = v2Input();
    const v1 = buildSnapshot(input);
    const v2 = buildSnapshot(input, v2Input()[1]);

    // 0..8 magic, 20..32 row size and generation, 40..56 provenance.
    expect(Array.from(v2.subarray(0, 8))).toEqual(Array.from(v1.subarray(0, 8)));
    expect(Array.from(v2.subarray(20, 24))).toEqual(Array.from(v1.subarray(20, 24)));
    expect(Array.from(v2.subarray(24, 32))).toEqual(Array.from(v1.subarray(24, 32)));
    expect(Array.from(v2.subarray(40, 56))).toEqual(Array.from(v1.subarray(40, 56)));
    // The row itself is byte-identical: the header grew, the row did not.
    expect(Array.from(v2.subarray(80, 112))).toEqual(Array.from(v1.subarray(64, 96)));
  });

  it('tiles the list records and rows in id order', () => {
    const lists = [
      playlist({
        id: 0xfedcba9876543210n,
        name: 'A side',
        createdAt: 1_700_000_001,
        rows: ['B/one.mp3', 'B/two.mp3'],
      }),
      playlist({
        id: 0x0123456789abcdefn,
        name: 'B side',
        createdAt: 1_700_000_002,
        rows: ['A/three.mp3'],
      }),
    ];
    const image = buildSnapshot(input([]), lists);
    const areas = v2Areas(image);

    expect(areas.headerSize).toBe(HEADER_SIZE_V2);
    expect(areas.listCount).toBe(2);
    expect(areas.listRows).toBe(3);
    expect(image.byteLength).toBe(
      areas.headerSize + areas.poolSize + 2 * LIST_SIZE + 3 * LIST_ROW_SIZE,
    );
    expect(areas.listRowsStart + areas.listRows * LIST_ROW_SIZE).toBe(image.byteLength);

    const first = listRecord(image, 0);
    const second = listRecord(image, 1);
    expect(first.id).toBe(0x0123456789abcdefn);
    expect(second.id).toBe(0xfedcba9876543210n);
    expect(first.id < second.id).toBe(true);

    // Tiling: the first starts at 0, each start is the previous plus its count.
    expect(first.rowStart).toBe(0);
    expect(first.rowCount).toBe(1);
    expect(second.rowStart).toBe(first.rowStart + first.rowCount);
    expect(second.rowCount).toBe(2);
    expect(second.rowStart + second.rowCount).toBe(areas.listRows);

    expect(first.created).toBe(1_700_000_002n);
    expect(second.created).toBe(1_700_000_001n);
    expect(first.reserved).toEqual([0, 0, 0, 0]);
    expect(second.reserved).toEqual([0, 0, 0, 0]);

    // §7: the pool carries every name and path, and each offset resolves through it.
    expect(poolString(image, first.nameOffset)).toBe('B side');
    expect(poolString(image, second.nameOffset)).toBe('A side');

    const expectedPaths = ['A/three.mp3', 'B/one.mp3', 'B/two.mp3'];
    for (let index = 0; index < expectedPaths.length; index += 1) {
      const stored = listRow(image, index);
      expect(stored.bytes).toHaveLength(4);
      expect(poolString(image, stored.pathOffset)).toBe(expectedPaths[index]);
    }
  });

  it('writes a row for a Track the Snapshot has no measurements for', () => {
    const image = buildSnapshot(canonicalInput(), [
      playlist({ id: 7n, name: 'Unmeasured', rows: ['Missing/not-measured.mp3'] }),
    ]);

    expect(viewOf(image).getUint32(16, true)).toBe(1); // still one measurement row
    expect(poolString(image, listRow(image, 0).pathOffset)).toBe('Missing/not-measured.mp3');
  });

  it('accepts an id as a bigint, a decimal string or hex', () => {
    const id = 0x0123456789abcdefn;
    const forms: Array<bigint | string> = [
      id,
      '81985529216486895',
      '0123456789abcdef',
      '0x0123456789abcdef',
      '0X0123456789ABCDEF',
    ];
    const images = forms.map((form) =>
      buildSnapshot(input([]), [playlist({ id: form, name: 'One', rows: [] })]),
    );

    for (const image of images) expect(Array.from(image)).toEqual(Array.from(images[0]));
    expect(listRecord(images[0], 0).id).toBe(id);
  });

  it('is byte-identical across builds and across input order', () => {
    const first = playlist({ id: 2n, name: 'Two', rows: ['x/1.mp3'] });
    const second = playlist({ id: 1n, name: 'One', rows: ['x/2.mp3', 'x/3.mp3'] });

    const forwards = buildSnapshot(canonicalInput(), [first, second]);
    const backwards = buildSnapshot(canonicalInput(), [second, first]);
    const again = buildSnapshot(canonicalInput(), [first, second]);

    expect(Array.from(backwards)).toEqual(Array.from(forwards));
    expect(Array.from(again)).toEqual(Array.from(forwards));
  });

  it('keeps writing version 1 when there are no Playlists', () => {
    const implicit = buildSnapshot(canonicalInput());
    const explicit = buildSnapshot(canonicalInput(), []);
    const view = viewOf(implicit);

    expect(Array.from(explicit)).toEqual(Array.from(implicit));
    expect(view.getUint16(8, true)).toBe(1);
    expect(view.getUint16(10, true)).toBe(64);
    expect(implicit.byteLength).toBe(124);
    // The canonical fixture's CRC and length, unchanged from before version 2.
    expect(view.getUint32(36, true)).toBe(0x17ef7ef3);
    expect(crc32(implicit.subarray(HEADER_SIZE))).toBe(0x17ef7ef3);
  });

  it('refuses two lists with one id, and an id of zero', () => {
    const duplicate = playlist({ id: 5n, name: 'Same', rows: [] });

    expect(() => buildSnapshot(input([]), [duplicate, { ...duplicate, name: 'Other' }])).toThrow(
      /duplicate Playlist id/,
    );
    expect(() => buildSnapshot(input([]), [playlist({ id: 0n, name: 'Zero', rows: [] })])).toThrow(
      /id of zero/,
    );
    expect(() => buildSnapshot(input([]), [playlist({ id: '0', name: 'Zero', rows: [] })])).toThrow(
      /id of zero/,
    );
  });

  it('refuses more than 64 lists', () => {
    const many = Array.from({ length: 65 }, (_unused, index) =>
      playlist({ id: BigInt(index + 1), name: `List ${index}`, rows: [] }),
    );

    expect(() => buildSnapshot(input([]), many)).toThrow(/allows 64/);
  });

  it('refuses a name of 64 bytes and accepts 63', () => {
    expect(() =>
      buildSnapshot(input([]), [playlist({ id: 1n, name: 'x'.repeat(64), rows: [] })]),
    ).toThrow(/allows 63/);

    const image = buildSnapshot(input([]), [
      playlist({ id: 1n, name: 'x'.repeat(63), rows: [] }),
    ]);
    expect(poolString(image, listRecord(image, 0).nameOffset)).toBe('x'.repeat(63));
  });

  it('refuses more than 8192 Playlist rows', () => {
    const rows = Array.from({ length: 8193 }, (_unused, index) => `t/${index}.mp3`);

    expect(() => buildSnapshot(input([]), [playlist({ id: 1n, name: 'Huge', rows })])).toThrow(
      /allows 8192/,
    );
  });

  it('refuses a list row that is not a path the Library can look up', () => {
    const cases = ['/absolute.mp3', 'a//b.mp3', 'a/./b.mp3', 'a/../b.mp3', 'a\\b.mp3', ''];

    for (const path of cases) {
      expect(() =>
        buildSnapshot(input([]), [playlist({ id: 1n, name: 'Bad', rows: [path] })]),
      ).toThrow(/row 0/);
    }
  });

  it('derives a stable, non-zero id from a name and a time', () => {
    const id = playlistId('Night drive', 1_710_000_000);

    expect(id).toBe(playlistId('Night drive', 1_710_000_000));
    expect(id).not.toBe(0n);
    expect(id).not.toBe(playlistId('Night drive', 1_710_000_001));
    expect(id).not.toBe(playlistId('Day drive', 1_710_000_000));
    // Once kept, the id outlives the name it came from.
    const image = buildSnapshot(input([]), [
      playlist({ id, name: 'Night drive, renamed', rows: [] }),
    ]);
    expect(listRecord(image, 0).id).toBe(id);
  });
});
