import { describe, expect, it } from 'vitest';
import type { McpAnalysis, McpTrack } from '../src/device/mcp';
import type { ImageMeasurement } from '../src/domain/snapshot';
import { trackLengthLabel, trackLengthSeconds } from '../src/ui/track-length';

function track(durationMs: number | null): McpTrack {
  const analysis: McpAnalysis = {
    durationMs,
    bpm: null,
    key: null,
    energy: null,
    danceability: null,
    valence: null,
    arousal: null,
    mood: [],
    confidence: null,
    tagged: [],
    title: null,
    artist: null,
    album: null,
    genre: null,
  };
  return { name: 'a.mp3', path: 'A/a.mp3', size: 1, format: 'mp3', analysis };
}

const image = (durationMs: number | null): ImageMeasurement => ({
  bpm: null,
  key: null,
  durationMs,
});

describe('a track length in a row', () => {
  it('prints the device answer', () => {
    expect(trackLengthLabel(track(225000), null)).toBe('3:45');
  });

  it('falls back to the browser image when the card is not in the player', () => {
    expect(trackLengthLabel(undefined, image(10500))).toBe('0:11');
  });

  it('prefers the device answer when both exist', () => {
    expect(trackLengthLabel(track(180000), image(60000))).toBe('3:00');
  });

  it('prints nothing when neither measured it', () => {
    expect(trackLengthLabel(track(null), null)).toBeNull();
    expect(trackLengthLabel(undefined, image(0))).toBeNull();
    expect(trackLengthLabel(null, null)).toBeNull();
    expect(trackLengthSeconds(track(null), image(null))).toBeNull();
  });

});
