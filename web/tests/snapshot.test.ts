import { crc32 } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import type { Analysis } from '../src/domain/analysis';
import {
  buildSnapshot,
  fnv1a64,
  imageRowKey,
  playlistId,
  readImageMeasurements,
  snapshotPath,
  type SnapshotInput,
  type SnapshotPlaylist,
  type SnapshotTrack,
} from '../src/domain/snapshot';
import { sampleAnalysis } from './sampleAnalysis';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

const HEADER_SIZE = 80;
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
 * has a distinct, unambiguous encoding: 124.54 BPM rounds to 1245 (code 546),
 * 0.784 energy to 78, and the four interned strings are 28 bytes.
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

/** A 24-bit little-endian integer: the width of a row's four pool offsets. */
function readUint24(bytes: Uint8Array, at: number): number {
  return bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16);
}

/** The measurement word of one row, unpacked from bit 0 exactly as §4 lays it out. */
function measurements(image: Uint8Array, index: number) {
  const word = viewOf(image).getBigUint64(rowOffset(index) + 24, true);
  return {
    bpmCode: Number(word & 0x7ffn),
    key: Number((word >> 11n) & 0x1fn),
    energy: Number((word >> 16n) & 0x7fn),
    danceability: Number((word >> 23n) & 0x7fn),
    valence: Number((word >> 30n) & 0x7fn),
    arousal: Number((word >> 37n) & 0x7fn),
    confidence: Number((word >> 44n) & 0x7fn),
    moodBits: Number((word >> 51n) & 0x1fn),
    sourceFlags: Number((word >> 56n) & 0x3fn),
    reservedBits: Number(word >> 62n),
  };
}

/** The four strings a row points at, resolved through the pool's offset region. */
function rowStrings(image: Uint8Array, index: number, headerSize = HEADER_SIZE): string[] {
  const view = viewOf(image);
  const poolStart = headerSize + view.getUint32(16, true) * ROW_SIZE;
  const row = rowOffset(index, headerSize);
  return [0, 1, 2, 3].map((field) =>
    readString(image, poolStart + readUint24(image, row + 12 + field * 3)).value,
  );
}

describe('reading an image back', () => {
  it('answers the same numbers it wrote, keyed by path and size', () => {
    const rows = readImageMeasurements(buildSnapshot(canonicalInput()));

    expect(rows.get(imageRowKey('Artist/Album/01 - Track.mp3', 4_000_000))).toEqual({
      bpm: 124.5,
      key: '8A',
    });
  });

  it('finds nothing under a different size — the same file is part of the binding', () => {
    const rows = readImageMeasurements(buildSnapshot(canonicalInput()));

    expect(rows.get(imageRowKey('Artist/Album/01 - Track.mp3', 4_000_001))).toBeUndefined();
    expect(rows.get(imageRowKey('Artist/Album/02.mp3', 4_000_000))).toBeUndefined();
  });

  it('says a row has no number when it was written without one', () => {
    const image = buildSnapshot(
      input([
        {
          path: 'CARD/music/A/b.mp3',
          size: 1,
          title: '',
          artist: '',
          album: '',
          genre: '',
          analysis: analysis({ tempo: { bpm: 0 }, key: { camelot: '' } }),
        },
      ]),
    );

    expect(readImageMeasurements(image).get(imageRowKey('A/b.mp3', 1))).toEqual({
      bpm: null,
      key: null,
    });
  });

  it('refuses what is not an image', () => {
    expect(() => readImageMeasurements(new Uint8Array(64))).toThrow(/shorter than its header/);

    const wrongVersion = buildSnapshot(canonicalInput());
    new DataView(wrongVersion.buffer).setUint16(8, 2, true);
    expect(() => readImageMeasurements(wrongVersion)).toThrow(/unknown format version/);
  });
});

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
    expect(snapshotPath('MYCARD/music')).toBeNull();
    expect(snapshotPath('MYCARD/music/')).toBeNull();
  });

  it('matches the music directory whatever case the card formatted it with', () => {
    // A card is FAT32: `MUSIC` and `music` are one directory, and `isCardRoot` accepts
    // either. Refusing the path here would accept the card and then give every track on
    // it no Snapshot path at all — measured, and nowhere to be written.
    expect(snapshotPath('MYCARD/Music/Album/x.mp3')).toBe('Album/x.mp3');
    expect(snapshotPath('MYCARD/MUSIC/Album/x.mp3')).toBe('Album/x.mp3');
    expect(snapshotPath('Music/Album/x.mp3')).toBe('Album/x.mp3');
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
  it('writes the version 3 header, one row, and an empty list area', () => {
    const image = buildSnapshot(canonicalInput());
    const view = viewOf(image);

    expect(decoder.decode(image.subarray(0, 8))).toBe('K7SNAP\0\0');
    expect(view.getUint16(8, true)).toBe(3);
    expect(view.getUint16(10, true)).toBe(80);
    expect(image.byteLength).toBe(140);
    expect(view.getUint32(12, true)).toBe(image.byteLength);
    expect(view.getUint32(16, true)).toBe(1);
    expect(view.getUint32(20, true)).toBe(32);
    expect(view.getBigUint64(24, true)).toBe(1_700_000_000n);
    expect(view.getUint32(32, true)).toBe(28);
    expect(view.getUint32(40, true)).toBe(10_400);
    expect(view.getUint32(44, true)).toBe(1);
    expect(view.getBigUint64(48, true)).toBe(0x0123456789abcdefn);
    expect(view.getBigUint64(56, true)).toBe(0n);

    // §3: the list fields are written even when `list_count` is 0.
    expect(view.getUint16(64, true)).toBe(0);
    expect(view.getUint16(66, true)).toBe(32);
    expect(view.getUint16(68, true)).toBe(4);
    expect(view.getUint16(70, true)).toBe(0);
    expect(view.getUint32(72, true)).toBe(0);
    expect(view.getUint32(76, true)).toBe(28);

    const row = rowOffset(0);
    expect(view.getBigUint64(row, true)).toBe(0x1b664b4fc563e884n);
    expect(view.getBigUint64(row, true)).toBe(
      fnv1a64(encoder.encode('Artist/Album/01 - Track.mp3')),
    );
    expect(view.getUint32(row + 8, true)).toBe(4_000_000);
    expect(readUint24(image, row + 12)).toBe(0);
    expect(readUint24(image, row + 15)).toBe(10);
    expect(readUint24(image, row + 18)).toBe(17);
    expect(readUint24(image, row + 21)).toBe(23);

    expect(measurements(image, 0)).toEqual({
      bpmCode: 546, // 124.5 x 10 - 699
      key: 8, // 8A
      energy: 78,
      danceability: 50,
      valence: 40,
      arousal: 60,
      confidence: 90,
      moodBits: 0x08, // relaxed
      sourceFlags: 0x10,
      reservedBits: 0,
    });
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

    expect(measurements(image, 0).bpmCode).toBe(1245 - 699);
    expect(measurements(image, 0).energy).toBe(78);
  });

  it('packs the BPM endpoints as 1 and 1101', () => {
    const codeAt = (bpm: number): number =>
      measurements(
        buildSnapshot(
          input([
            track({
              path: `CARD/music/bpm-${bpm}.mp3`,
              analysis: analysis({ tempo: { bpm } }),
            }),
          ]),
        ),
        0,
      ).bpmCode;

    expect(codeAt(70)).toBe(1);
    expect(codeAt(180)).toBe(1101);
    expect(codeAt(124.5)).toBe(546);
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

    expect(measurements(image, 0)).toEqual({
      bpmCode: 0,
      key: 0,
      energy: 127,
      danceability: 127,
      valence: 127,
      arousal: 127,
      confidence: 127,
      moodBits: 0,
      sourceFlags: 0x07,
      reservedBits: 0,
    });
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

    expect(measurements(image, 0).moodBits).toBe(0x01 | 0x02 | 0x08 | 0x10);
    expect(measurements(image, 0).danceability).toBe(90);
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

    expect(measurements(image, 0).energy).toBe(70);
    expect(measurements(image, 0).sourceFlags).toBe(0x01 | 0x02 | 0x08 | 0x10 | 0x20);
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
      expect(measurements(image, 0).key).toBe(expected);
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

  it('interns the row strings and lets two rows share one offset', () => {
    const shared = { artist: 'Shared Artist', album: 'Shared Album', genre: 'House' };
    const image = buildSnapshot(
      input([
        track({ path: 'CARD/music/A/one.mp3', title: 'One', ...shared }),
        track({ path: 'CARD/music/B/two.mp3', title: 'Two', ...shared }),
      ]),
    );
    const view = viewOf(image);

    // 'One', 'Two', 'Shared Artist', 'Shared Album', 'House', each NUL-terminated.
    expect(view.getUint32(32, true)).toBe(4 + 4 + 14 + 13 + 6);
    expect(image.byteLength).toBe(80 + 2 * 32 + 41);

    const offsets = [0, 1].map((index) =>
      [0, 1, 2, 3].map((field) => readUint24(image, rowOffset(index) + 12 + field * 3)),
    );
    const first = offsets[0];
    const second = offsets[1];

    expect(first[0]).not.toBe(second[0]); // two different titles
    for (const field of [1, 2, 3]) expect(first[field]).toBe(second[field]);
    expect(new Set([...first, ...second]).size).toBe(5);
    expect(rowStrings(image, 0).slice(1)).toEqual(rowStrings(image, 1).slice(1));
  });

  it('stamps the header with the CRC over the rows and the row pool', () => {
    const image = buildSnapshot(canonicalInput());

    // Independently assembled from §3/§4: 32 row bytes + 28 pool bytes, CRC 0xd71cec22.
    expect(crc32(image.subarray(HEADER_SIZE))).toBe(0xd71cec22);
    expect(viewOf(image).getUint32(36, true)).toBe(0xd71cec22);
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
 * Lists (§8 of docs/specs/snapshot-format.md)
 * ------------------------------------------------------------------ */

interface ImageAreas {
  headerSize: number;
  rowCount: number;
  poolStart: number;
  poolSize: number;
  rowPoolSize: number;
  listCount: number;
  listRows: number;
  listsStart: number;
  listRowsStart: number;
}

/** The five regions, read back through the header exactly as the C reader would. */
function areas(image: Uint8Array): ImageAreas {
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
    rowCount,
    poolStart,
    poolSize,
    rowPoolSize: view.getUint32(76, true),
    listCount,
    listRows,
    listsStart,
    listRowsStart: listsStart + listCount * LIST_SIZE,
  };
}

function listRecord(image: Uint8Array, index: number) {
  const view = viewOf(image);
  const at = areas(image).listsStart + index * LIST_SIZE;
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
  const at = areas(image).listRowsStart + index * LIST_ROW_SIZE;
  return { pathOffset: view.getUint32(at, true), bytes: Array.from(image.subarray(at, at + 4)) };
}

/** A pool string at an offset relative to the pool, as both offsets in a record are. */
function poolString(image: Uint8Array, relativeOffset: number): string {
  return readString(image, areas(image).poolStart + relativeOffset).value;
}

function playlist(
  overrides: Pick<SnapshotPlaylist, 'id' | 'name' | 'rows'> &
    Partial<Pick<SnapshotPlaylist, 'createdAt'>>,
): SnapshotPlaylist {
  return { createdAt: 1_710_000_000, ...overrides };
}

describe('snapshot images with Playlists', () => {
  /**
   * The fixture. Its numbers are worked out from the format rather than from the
   * writer: the canonical track's four interned strings are 28 bytes, the name is 12
   * with its NUL, the two paths are 28 and 29, so the pool is 97 and the image is
   * 80 + 32 + 97 + 32 + 2*4 = 249.
   */
  function listInput(): [SnapshotInput, SnapshotPlaylist[]] {
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

  it('writes the version 3 header and the list area at its offsets', () => {
    const [input, lists] = listInput();
    const image = buildSnapshot(input, lists);
    const view = viewOf(image);

    expect(decoder.decode(image.subarray(0, 8))).toBe('K7SNAP\0\0');
    expect(view.getUint16(8, true)).toBe(3);
    expect(view.getUint16(10, true)).toBe(80);
    expect(image.byteLength).toBe(249);
    expect(view.getUint32(12, true)).toBe(249);
    expect(view.getUint32(16, true)).toBe(1);
    expect(view.getUint32(20, true)).toBe(32);
    expect(view.getUint32(32, true)).toBe(97);
    expect(view.getBigUint64(56, true)).toBe(0n);

    // §3, bytes 64..80.
    expect(view.getUint16(64, true)).toBe(1);
    expect(view.getUint16(66, true)).toBe(32);
    expect(view.getUint16(68, true)).toBe(4);
    expect(view.getUint16(70, true)).toBe(0);
    expect(view.getUint32(72, true)).toBe(2);
    expect(view.getUint32(76, true)).toBe(28);

    expect(view.getUint32(36, true)).toBe(crc32(image.subarray(HEADER_SIZE)));
  });

  it('keeps the row, its strings and the provenance apart from the list area', () => {
    const [input, lists] = listInput();
    const noLists = buildSnapshot(input);
    const withLists = buildSnapshot(input, lists);

    // 0..12 magic, version and header size; 16..32 row count, row size, generation;
    // 40..64 provenance and reserved. Only the sizes, the list fields and the CRC move.
    expect(Array.from(withLists.subarray(0, 12))).toEqual(Array.from(noLists.subarray(0, 12)));
    expect(Array.from(withLists.subarray(16, 32))).toEqual(Array.from(noLists.subarray(16, 32)));
    expect(Array.from(withLists.subarray(40, 64))).toEqual(Array.from(noLists.subarray(40, 64)));
    // The rows and the whole row pool are byte-identical, at the same offsets.
    expect(Array.from(withLists.subarray(80, 140))).toEqual(Array.from(noLists.subarray(80, 140)));
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
    const imageAreas = areas(image);

    expect(imageAreas.headerSize).toBe(HEADER_SIZE);
    expect(imageAreas.listCount).toBe(2);
    expect(imageAreas.listRows).toBe(3);
    expect(image.byteLength).toBe(
      imageAreas.headerSize + imageAreas.poolSize + 2 * LIST_SIZE + 3 * LIST_ROW_SIZE,
    );
    expect(imageAreas.listRowsStart + imageAreas.listRows * LIST_ROW_SIZE).toBe(image.byteLength);

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
    expect(second.rowStart + second.rowCount).toBe(imageAreas.listRows);

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

  it('interns list strings among themselves and never with a row string', () => {
    const image = buildSnapshot(canonicalInput(), [
      playlist({ id: 1n, name: 'Artist', rows: ['Shared/p.mp3'] }),
      playlist({ id: 2n, name: 'Other', rows: ['Shared/p.mp3'] }),
    ]);
    const imageAreas = areas(image);
    const first = listRecord(image, 0);
    const second = listRecord(image, 1);

    // The row pool's 'Artist' at offset 10 is not reused: 'Artist\0' is 7 more bytes,
    // then the first list's path ('Shared/p.mp3\0', 13), then 'Other\0' (6); the
    // second list's repeated path reuses the first list's offset.
    expect(imageAreas.rowPoolSize).toBe(28);
    expect(imageAreas.poolSize).toBe(28 + 7 + 13 + 6);
    expect(first.nameOffset).toBe(28);
    expect(second.nameOffset).toBe(48);
    expect(listRow(image, 0).pathOffset).toBe(35);
    expect(listRow(image, 1).pathOffset).toBe(35);
    expect(poolString(image, 35)).toBe('Shared/p.mp3');
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

  it('still writes version 3, with list_count 0, when there are no Playlists', () => {
    const implicit = buildSnapshot(canonicalInput());
    const explicit = buildSnapshot(canonicalInput(), []);
    const view = viewOf(implicit);

    expect(Array.from(explicit)).toEqual(Array.from(implicit));
    expect(view.getUint16(8, true)).toBe(3);
    expect(view.getUint16(10, true)).toBe(80);
    expect(view.getUint16(64, true)).toBe(0);
    expect(view.getUint16(66, true)).toBe(32);
    expect(view.getUint16(68, true)).toBe(4);
    expect(view.getUint32(72, true)).toBe(0);
    expect(view.getUint32(76, true)).toBe(28);
    expect(implicit.byteLength).toBe(140);
    expect(view.getUint32(36, true)).toBe(crc32(implicit.subarray(HEADER_SIZE)));
    expect(view.getUint32(36, true)).toBe(0xd71cec22);
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
