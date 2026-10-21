import { readFile } from 'node:fs/promises';
import { parseBlob } from 'music-metadata';
import { describe, expect, it } from 'vitest';
import { readTrackMetadata } from '../src/audio/metadata';
import { writeAnalysisTags } from '../src/audio/tagWriter';
import { sampleAnalysis } from './sampleAnalysis';

const fixtures = {
  mp3: new URL('./fixtures/tagged.mp3', import.meta.url),
  flac: new URL('./fixtures/plain.flac', import.meta.url),
};

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
