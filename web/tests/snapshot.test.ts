import { crc32 } from 'node:zlib';
import { describe, expect, it } from 'vitest';
import type { Analysis } from '../src/domain/analysis';
import {
  buildSnapshot,
  fnv1a64,
  snapshotPath,
  type SnapshotInput,
  type SnapshotTrack,
} from '../src/domain/snapshot';
import { sampleAnalysis } from './sampleAnalysis';

const encoder = new TextEncoder();
const decoder = new TextDecoder();

const HEADER_SIZE = 64;
const ROW_SIZE = 32;

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

function rowOffset(index: number): number {
  return HEADER_SIZE + index * ROW_SIZE;
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
function rowStrings(image: Uint8Array, index: number): string[] {
  const view = viewOf(image);
  const poolStart = HEADER_SIZE + view.getUint32(16, true) * ROW_SIZE;
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

    expect(decoder.decode(image.subarray(0, 8))).toBe('XTAPESNP');
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
