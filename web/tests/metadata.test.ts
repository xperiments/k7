import { readFile } from 'node:fs/promises';
import { describe, expect, it } from 'vitest';
import { readTrackMetadata } from '../src/audio/metadata';

const fixture = new URL('./fixtures/tagged.mp3', import.meta.url);
const moodFixture = new URL('./fixtures/tagged-mood.mp3', import.meta.url);
const genreFixture = new URL('./fixtures/tagged-genre.mp3', import.meta.url);

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

  it('reads the names and genre the Process row and the Snapshot show', async () => {
    const bytes = new Uint8Array(await readFile(genreFixture));
    const file = new File([bytes], 'tagged-genre.mp3', { type: 'audio/mpeg' });

    const metadata = await readTrackMetadata(file);

    expect(metadata.tags).toEqual({
      title: 'Tape Test',
      artist: 'The Reels',
      album: 'Deck One',
      genre: 'House',
    });
    // The measurements in the same file are unaffected by the tags around them.
    expect(metadata.bpm).toBe(128);
    expect(metadata.key?.camelot).toBe('8A');
  });

  it('carries empty names rather than nothing for a file with no usable tags', async () => {
    const file = new File([new Uint8Array([1, 2, 3])], 'broken.mp3', {
      type: 'audio/mpeg',
    });

    const metadata = await readTrackMetadata(file);

    expect(metadata.bpm).toBeUndefined();
    expect(metadata.key).toBeUndefined();
    expect(metadata.stored).toBeUndefined();
    // Present and empty, so the row shows a dash and the Snapshot export does not re-read
    // a file whose tags the page already looked at.
    expect(metadata.tags).toEqual({ title: '', artist: '', album: '', genre: '' });
  });
});
