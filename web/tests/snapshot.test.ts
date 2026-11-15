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
  type SnapshotTrack,
} from '../src/domain/snapshot';
import { sampleAnalysis } from './sampleAnalysis';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

const HEADER_SIZE = 80;
const ROW_SIZE = 36;

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
  const word = viewOf(image).getBigUint64(rowOffset(index) + 28, true);
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
    readString(image, poolStart + readUint24(image, row + 16 + field * 3)).value,
  );
}

describe('reading an image back', () => {
  it('answers the same numbers it wrote, keyed by path and size', () => {
    const rows = readImageMeasurements(buildSnapshot(canonicalInput()));

    expect(rows.get(imageRowKey('Artist/Album/01 - Track.mp3', 4_000_000))).toEqual({
      bpm: 124.5,
      key: '8A',
      durationMs: 8000,
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
      durationMs: 8000,
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
  it('writes the version 5 header and one row, with no list area', () => {
    const image = buildSnapshot(canonicalInput());
    const view = viewOf(image);

    expect(decoder.decode(image.subarray(0, 8))).toBe('K7SNAP\0\0');
    expect(view.getUint16(8, true)).toBe(5);
    expect(view.getUint16(10, true)).toBe(80);
    expect(image.byteLength).toBe(144);
    expect(view.getUint32(12, true)).toBe(image.byteLength);
    expect(view.getUint32(16, true)).toBe(1);
    expect(view.getUint32(20, true)).toBe(36);
    expect(view.getBigUint64(24, true)).toBe(1_700_000_000n);
    expect(view.getUint32(32, true)).toBe(28);
    expect(view.getUint32(40, true)).toBe(10_400);
    expect(view.getUint32(44, true)).toBe(1);
    expect(view.getBigUint64(48, true)).toBe(0x0123456789abcdefn);
    /* Bytes 56..80 are reserved and zero: 64..80 held the v4 list area's counts. */
    for (let at = 56; at < 80; at += 8) {
      expect(view.getBigUint64(at, true)).toBe(0n);
    }

    const row = rowOffset(0);
    expect(view.getBigUint64(row, true)).toBe(0x1b664b4fc563e884n);
    expect(view.getBigUint64(row, true)).toBe(
      fnv1a64(encoder.encode('Artist/Album/01 - Track.mp3')),
    );
    expect(view.getUint32(row + 8, true)).toBe(8000);
    expect(view.getUint32(row + 12, true)).toBe(4_000_000);
    expect(readUint24(image, row + 16)).toBe(0);
    expect(readUint24(image, row + 19)).toBe(10);
    expect(readUint24(image, row + 22)).toBe(17);
    expect(readUint24(image, row + 25)).toBe(23);

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
    expect(image.byteLength).toBe(80 + 2 * 36 + 41);

    const offsets = [0, 1].map((index) =>
      [0, 1, 2, 3].map((field) => readUint24(image, rowOffset(index) + 16 + field * 3)),
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

    // Independently assembled from §3/§4: 36 row bytes + 28 pool bytes, CRC 0x666e87f1.
    expect(crc32(image.subarray(HEADER_SIZE))).toBe(0x666e87f1);
    expect(viewOf(image).getUint32(36, true)).toBe(0x666e87f1);
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

describe('playlist ids', () => {
  it('derives a stable, non-zero id from a name and a time', () => {
    const id = playlistId('Night drive', 1_710_000_000);

    expect(id).toBe(playlistId('Night drive', 1_710_000_000));
    expect(id).not.toBe(0n);
    expect(id).not.toBe(playlistId('Night drive', 1_710_000_001));
    expect(id).not.toBe(playlistId('Day drive', 1_710_000_000));
  });
});
