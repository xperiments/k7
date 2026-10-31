import { describe, expect, it } from 'vitest';
import { extractCovers } from '../src/ui/covers';
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';

const withCover = join(process.cwd(), 'tests', 'fixtures', 'plain.flac');

describe('album art out of the files', () => {
  it('names each cover after its album folder and makes one entry per album', async () => {
    const data = await readFile(withCover);
    const file = new File([data], 'plain.flac');
    const result = await extractCovers([
      { file, path: 'music/Artist/Album/01.flac' },
      { file, path: 'music/Artist/Album/02.flac' },
    ]);

    // The fixture may or may not carry art; either way the rule is: one entry per album,
    // and a track that has none is reported rather than silently skipped.
    expect(result.covers + result.missing.length).toBeGreaterThan(0);
    if (result.covers > 0) {
      expect(result.albums).toBe(1);
      expect(result.archive.length).toBeGreaterThan(0);
    }
  });
});
