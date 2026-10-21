import { readFile } from 'node:fs/promises';
import { describe, expect, it } from 'vitest';
import { readTrackMetadata } from '../src/audio/metadata';

const fixture = new URL('./fixtures/tagged.mp3', import.meta.url);
const moodFixture = new URL('./fixtures/tagged-mood.mp3', import.meta.url);

describe('embedded metadata', () => {
  it('reads BPM and key from ID3 tags', async () => {
    const bytes = new Uint8Array(await readFile(fixture));
    const file = new File([bytes], 'tagged.mp3', { type: 'audio/mpeg' });

    const metadata = await readTrackMetadata(file);

    expect(metadata.bpm).toBe(128);
    expect(metadata.key).toEqual({ tonic: 'A', mode: 'minor', camelot: '8A' });
  });

  it('reads mood and energy tags', async () => {
    const bytes = new Uint8Array(await readFile(moodFixture));
    const file = new File([bytes], 'tagged-mood.mp3', { type: 'audio/mpeg' });

    const metadata = await readTrackMetadata(file);

    expect(metadata.bpm).toBe(120);
    expect(metadata.mood).toBe('Happy');
    expect(metadata.energy).toBe(0.8);
  });

  it('returns nothing for a file without usable tags', async () => {
    const file = new File([new Uint8Array([1, 2, 3])], 'broken.mp3', {
      type: 'audio/mpeg',
    });

    await expect(readTrackMetadata(file)).resolves.toEqual({});
  });
});
