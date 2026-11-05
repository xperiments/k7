import { readFile } from 'node:fs/promises';
import { parseBlob } from 'music-metadata';
import { describe, expect, it } from 'vitest';
import { TagLib } from 'taglib-wasm';
import { imageSize } from '../src/audio/covers';
import { readTrackMetadata } from '../src/audio/metadata';
import { writeAnalysisTags, writeTrackLabel, writeTrackMetadata } from '../src/audio/tagWriter';
import { sampleAnalysis } from './sampleAnalysis';

const fixtures = {
  mp3: new URL('./fixtures/tagged.mp3', import.meta.url),
  flac: new URL('./fixtures/plain.flac', import.meta.url),
};

function fileOf(bytes: Uint8Array, name: string, type: string): File {
  const copy = new Uint8Array(bytes.byteLength);
  copy.set(bytes);
  return new File([copy], name, { type });
}

function nativeValue(
  metadata: Awaited<ReturnType<typeof parseBlob>>,
  id: string,
): unknown {
  for (const tags of Object.values(metadata.native)) {
    for (const tag of tags) {
      if (tag.id.toLowerCase() === id.toLowerCase()) return tag.value;
    }
  }
  return undefined;
}

describe('tag writing', () => {
  for (const format of ['mp3', 'flac'] as const) {
    it(`writes standard tags and the XTAPE manifest to ${format.toUpperCase()}`, async () => {
      const input = new Uint8Array(await readFile(fixtures[format]));
      const file = new File([input], `sample.${format}`, {
        type: format === 'mp3' ? 'audio/mpeg' : 'audio/flac',
      });

      const output = await writeAnalysisTags(file, sampleAnalysis);
      expect(output.byteLength).toBeGreaterThan(0);

      const buffer = new ArrayBuffer(output.byteLength);
      new Uint8Array(buffer).set(output);
      const written = new File([buffer], `written.${format}`, { type: file.type });
      const parsed = await parseBlob(written, { duration: true, skipCovers: true });
      const original = await parseBlob(file, { duration: true, skipCovers: true });

      expect(nativeValue(parsed, 'TBPM') ?? nativeValue(parsed, 'BPM')).toBe('128.4');
      expect(parsed.common.mood).toBe('Relaxed');
      expect(parsed.format.duration).toBeCloseTo(original.format.duration ?? 0, 1);

      const key = nativeValue(parsed, 'INITIALKEY') ?? nativeValue(parsed, 'TKEY');
      expect(key).toBe('Am');
      const energy = nativeValue(parsed, 'ENERGY') ?? nativeValue(parsed, 'TXXX:ENERGY');
      expect(energy).toBe('0.6');
      const manifest = nativeValue(parsed, 'XTAPE') ?? nativeValue(parsed, 'TXXX:XTAPE');
      expect(typeof manifest).toBe('string');

      const metadata = await readTrackMetadata(written);
      expect(metadata.bpm).toBeCloseTo(128.4, 2);
      expect(metadata.stored?.bpm).toBeCloseTo(128.4, 2);
      expect(metadata.stored?.key.camelot).toBe('8A');
      expect(metadata.stored?.moodTags?.relaxed).toBeCloseTo(0.7);
      expect(metadata.stored?.energy).toBeCloseTo(0.6);
    });
  }
});

describe('the automatic metadata write', () => {
  for (const format of ['mp3', 'flac'] as const) {
    it(`writes the measured tags to ${format.toUpperCase()} even without art, then leaves it`, async () => {
      const type = format === 'mp3' ? 'audio/mpeg' : 'audio/flac';
      const input = new Uint8Array(await readFile(fixtures[format]));
      const file = fileOf(input, `plain.${format}`, type);

      const first = await writeTrackMetadata(file, sampleAnalysis);
      expect(first.kind).toBe('written');
      const bytes = first.kind === 'written' ? first.bytes : input;

      const written = fileOf(bytes, `done.${format}`, type);
      const metadata = await readTrackMetadata(written);
      expect(metadata.bpm).toBeCloseTo(128.4, 2);
      expect(metadata.stored?.key.camelot).toBe('8A');
      expect(metadata.stored?.moodTags?.relaxed).toBeCloseTo(0.7);
      expect(metadata.stored?.energy).toBeCloseTo(0.6);

      const again = await writeTrackMetadata(
        fileOf(bytes, `done.${format}`, type),
        sampleAnalysis,
      );
      expect(again.kind).toBe('complete');
    });
  }
});

describe('the worn label picture', () => {
  const coverAsset = new URL('../../assets/labels/cover.jpg', import.meta.url);

  /** A JPEG header that declares a size; the writer only reads these bytes back by header. */
  function jpeg(width: number, height: number): Uint8Array {
    return new Uint8Array([
      0xff, 0xd8,
      0xff, 0xc0, 0x00, 0x11, 0x08,
      height >> 8, height & 0xff, width >> 8, width & 0xff,
      0x03,
      0x01, 0x11, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00,
      0xff, 0xd9,
    ]);
  }

  /** Stands in for the browser's canvas and the wear shader. */
  async function render(): Promise<Uint8Array> {
    return jpeg(402, 188);
  }

  function fileOf(bytes: Uint8Array, name: string, type: string): File {
    const copy = new Uint8Array(bytes.byteLength);
    copy.set(bytes);
    return new File([copy], name, { type });
  }

  for (const format of ['mp3', 'flac'] as const) {
    const type = format === 'mp3' ? 'audio/mpeg' : 'audio/flac';

    it(`adds k7label to ${format.toUpperCase()}, then leaves the file alone`, async () => {
      const input = new Uint8Array(await readFile(fixtures[format]));
      const art = new Uint8Array(await readFile(coverAsset));
      const taglib = await TagLib.initialize();

      // A Track with art of its own: that is what the two are made from.
      const withArt = await taglib.edit(input, (audio) => {
        audio.setPictures([{ type: 'FrontCover', mimeType: 'image/jpeg', data: art }]);
      });

      const written = await writeTrackLabel(fileOf(withArt, `art.${format}`, type), undefined, {
        render,
      });
      expect(written.kind).toBe('written');

      const done = written.kind === 'written' ? written.bytes : withArt;
      const parsed = await parseBlob(fileOf(done, `done.${format}`, type), {
        skipCovers: false,
        duration: false,
      });
      const pictures = parsed.common.picture ?? [];
      expect(pictures.map((picture) => picture.description ?? '')).toEqual(['', 'k7label']);
      expect(imageSize(pictures[1].data)).toEqual({ width: 402, height: 188 });

      // The next pass reads what the file carries and has nothing to do.
      const again = await writeTrackLabel(fileOf(done, `done.${format}`, type), undefined, {
        render,
      });
      expect(again.kind).toBe('complete');
    });
  }

  it('writes the measured tags and the label in one pass', async () => {
    const input = new Uint8Array(await readFile(fixtures.mp3));
    const art = new Uint8Array(await readFile(coverAsset));
    const taglib = await TagLib.initialize();
    const withArt = await taglib.edit(input, (audio) => {
      audio.setPictures([{ type: 'FrontCover', mimeType: 'image/jpeg', data: art }]);
    });

    const first = await writeTrackMetadata(
      fileOf(withArt, 'art.mp3', 'audio/mpeg'),
      sampleAnalysis,
      undefined,
      { render },
    );
    expect(first.kind).toBe('written');
    const done = first.kind === 'written' ? first.bytes : withArt;

    const parsed = await parseBlob(fileOf(done, 'done.mp3', 'audio/mpeg'), {
      skipCovers: false,
      duration: false,
    });
    const pictures = parsed.common.picture ?? [];
    expect(pictures.map((picture) => picture.description ?? '')).toEqual(['', 'k7label']);
    const metadata = await readTrackMetadata(fileOf(done, 'done.mp3', 'audio/mpeg'));
    expect(metadata.bpm).toBeCloseTo(128.4, 2);
    expect(metadata.stored?.key.camelot).toBe('8A');

    const again = await writeTrackMetadata(
      fileOf(done, 'done.mp3', 'audio/mpeg'),
      sampleAnalysis,
      undefined,
      { render },
    );
    expect(again.kind).toBe('complete');
  });

  it('replaces a label this build would not make any more', async () => {
    const input = new Uint8Array(await readFile(fixtures.mp3));
    const art = new Uint8Array(await readFile(coverAsset));
    const taglib = await TagLib.initialize();
    const withArt = await taglib.edit(input, (audio) => {
      audio.setPictures([{ type: 'FrontCover', mimeType: 'image/jpeg', data: art }]);
    });

    const first = await writeTrackLabel(fileOf(withArt, 'art.mp3', 'audio/mpeg'), undefined, {
      render,
    });
    const old = first.kind === 'written' ? first.bytes : withArt;

    // The same file, a build whose wear renders differently: the old picture must go.
    const other = await writeTrackLabel(fileOf(old, 'art.mp3', 'audio/mpeg'), undefined, {
      render: async () => jpeg(402, 189),
    });
    expect(other.kind).toBe('written');
    const done = other.kind === 'written' ? other.bytes : old;

    const parsed = await parseBlob(fileOf(done, 'done.mp3', 'audio/mpeg'), {
      skipCovers: false,
      duration: false,
    });
    const labels = (parsed.common.picture ?? []).filter(
      (picture) => picture.description === 'k7label',
    );
    expect(labels).toHaveLength(1);
    expect(imageSize(labels[0].data)).toEqual({ width: 402, height: 189 });
  });

  it('leaves a file with no art of its own alone: there is nothing to scale', async () => {
    const input = new Uint8Array(await readFile(fixtures.mp3));
    const untouched = await writeTrackLabel(fileOf(input, 'plain.mp3', 'audio/mpeg'), undefined, {
      render,
    });
    expect(untouched.kind).toBe('no-source');
  });
});
