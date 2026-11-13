import { describe, expect, it } from 'vitest';
import { ANALYSIS_SCHEMA, type Analysis, type MoodTag } from '../domain/analysis';
import { buildSnapshot, fnv1a64, snapshotPath, type SnapshotTrack } from '../domain/snapshot';
import { McpError } from '../device/mcp';
import type { CachedMedia } from '../storage/media';
import { keyCode, profileFromRows, readImageRows, searchImage } from './imageSource';

interface TrackOptions {
  path: string;
  title?: string;
  artist?: string;
  genre?: string;
  bpm?: number;
  camelot?: string;
  energy?: number;
  danceable?: number;
  valence?: number;
  arousal?: number;
  moods?: Partial<Record<Exclude<MoodTag, 'danceable'>, number>>;
}

function track(options: TrackOptions): SnapshotTrack {
  const {
    path,
    title = 'a',
    artist = '',
    genre = '',
    bpm = 120,
    camelot = '8A',
    energy = 0.5,
    danceable = 0.5,
    valence = 0.5,
    arousal = 0.5,
    moods = {},
  } = options;
  const tags: Record<MoodTag, number> = {
    happy: 0.1,
    sad: 0.1,
    aggressive: 0.1,
    relaxed: 0.1,
    party: 0.1,
    danceable,
  };
  for (const [mood, value] of Object.entries(moods)) tags[mood as MoodTag] = value ?? 0;
  const analysis: Analysis = {
    schema: ANALYSIS_SCHEMA,
    track: { name: path, path, size: 1, format: 'mp3', duration: 1, hash: 'h' },
    tempo: {
      bpm,
      rawBpm: bpm,
      confidence: 0.9,
      engine: 'fast',
      escalated: false,
      suggestEscalation: false,
      source: 'measured',
    },
    key: { tonic: 'A', mode: 'minor', camelot, strength: 0.5, source: 'measured' },
    mood: { tags, label: null, source: 'measured', valence, arousal },
    energy,
    energySource: 'measured',
    provenance: { analyzer: 'a', modelSet: 'm', window: null, analysedAt: '2026-01-01T00:00:00Z' },
  };
  return { path, size: 1, analysis, title, artist, album: '', genre };
}

const TRACKS = [
  track({
    path: 'music/a.mp3',
    title: 'Blue Monday',
    artist: 'New Order',
    genre: 'Techno',
    bpm: 120,
    camelot: '8A',
    energy: 0.5,
    valence: 0.2,
    arousal: 0.5,
    moods: { happy: 0.8 },
  }),
  track({
    path: 'music/b.mp3',
    title: 'Drift',
    artist: 'Boards of Canada',
    genre: 'Ambient',
    bpm: 90,
    camelot: '9B',
    energy: 0.2,
    valence: 0.9,
    arousal: 0.2,
    moods: { relaxed: 0.8 },
  }),
  track({
    path: 'music/c.mp3',
    title: 'Calm',
    artist: 'Boards of Canada',
    genre: 'Ambient',
    bpm: 140,
    camelot: '8A',
    energy: 0.9,
    valence: 0.5,
    arousal: 0.9,
    danceable: 0.9,
    moods: { party: 0.9 },
  }),
];

function image(): Uint8Array<ArrayBuffer> {
  return buildSnapshot(
    {
      tracks: TRACKS,
      generationSeconds: 7,
      analyserVersion: 1,
      appSchema: ANALYSIS_SCHEMA,
      modelSet: 1n,
    },
    [],
  );
}

function mediaRows(): CachedMedia[] {
  return TRACKS.map((each) => ({
    path: snapshotPath(each.path) ?? '',
    title: each.title,
    artist: each.artist,
    album: each.album,
    genre: each.genre,
    at: '2026-01-01',
    size: each.size,
  }));
}

describe('readImageRows', () => {
  it('reads the measurements and strings the producer wrote', () => {
    const rows = readImageRows(image());
    expect(rows).toHaveLength(3);
    const monday = rows.find((row) => row.title === 'Blue Monday');
    expect(monday?.bpmX10).toBe(1200);
    expect(monday?.key).toBe(8);
    expect(monday?.energy).toBe(50);
    expect(monday?.genre).toBe('Techno');
    expect(monday?.artist).toBe('New Order');
    expect(monday?.fingerprint).toBe(fnv1a64(new TextEncoder().encode('a.mp3')));
  });

  it('refuses bytes that are not a version 3 image', () => {
    expect(() => readImageRows(new Uint8Array(10))).toThrow(McpError);
  });
});

describe('keyCode', () => {
  it('reads Camelot and note names the way the device does', () => {
    expect(keyCode('8A')).toBe(8);
    expect(keyCode('8B')).toBe(20);
    expect(keyCode('Am')).toBe(8);
    expect(keyCode('A minor')).toBe(8);
    expect(keyCode('C')).toBe(20);
    expect(keyCode('C#m')).toBe(12);
    expect(keyCode('bogus')).toBe(0);
  });
});

describe('profileFromRows', () => {
  it('builds the library profile from an image that predates one', () => {
    const profile = profileFromRows(readImageRows(image()));
    expect(profile.tracks).toBe(3);
    expect(profile.genres).toEqual(['Ambient', 'Techno']);
    expect(profile.bpm?.min).toBe(90);
    expect(profile.bpm?.max).toBe(140);
    expect(profile.keys).toEqual(['8A', '9B']);
    expect(profile.moods).toEqual(['happy', 'relaxed', 'party']);
  });
});

describe('searchImage', () => {
  it('finds a track by the names the image carries', () => {
    const rows = readImageRows(image());
    expect(searchImage(rows, mediaRows(), { query: 'new order' }).tracks.map((t) => t.path)).toEqual([
      'a.mp3',
    ]);
    expect(searchImage(rows, mediaRows(), { query: 'boards' }).matched).toBe(2);
  });

  it('applies genre, bpm, key, mood and percentage filters', () => {
    const rows = readImageRows(image());
    expect(searchImage(rows, mediaRows(), { genre: 'ambient' }).matched).toBe(2);
    expect(searchImage(rows, mediaRows(), { bpmMin: 100, bpmMax: 130 }).tracks.map((t) => t.path)).toEqual([
      'a.mp3',
    ]);
    expect(searchImage(rows, mediaRows(), { key: ['Am'] }).tracks.map((t) => t.path)).toEqual([
      'a.mp3',
      'c.mp3',
    ]);
    expect(searchImage(rows, mediaRows(), { mood: ['party'] }).tracks.map((t) => t.path)).toEqual([
      'c.mp3',
    ]);
    expect(searchImage(rows, mediaRows(), { energyMin: 60 }).tracks.map((t) => t.path)).toEqual([
      'c.mp3',
    ]);
  });

  it('sorts and pages like the device', () => {
    const rows = readImageRows(image());
    expect(searchImage(rows, mediaRows(), { sort: 'bpm', limit: 2 }).tracks.map((t) => t.path)).toEqual([
      'b.mp3',
      'a.mp3',
    ]);
    expect(
      searchImage(rows, mediaRows(), { sort: 'bpm', offset: 1, limit: 1 }).tracks.map((t) => t.path),
    ).toEqual(['a.mp3']);
    expect(searchImage(rows, mediaRows(), { sort: 'title' }).tracks[0].path).toBe('a.mp3');
  });

  it('matches an unmeasured track on its path only', () => {
    const rows = readImageRows(image());
    const media = [
      ...mediaRows(),
      {
        path: 'z.mp3',
        title: 'Zed',
        artist: 'Nobody',
        album: '',
        genre: '',
        at: '2026-01-01',
        size: 1,
      },
    ];
    expect(searchImage(rows, media, { query: 'zed' }).matched).toBe(0);
    expect(searchImage(rows, media, { query: 'z.mp3' }).tracks[0].path).toBe('z.mp3');
    expect(searchImage(rows, media, { bpmMin: 1 }).tracks.map((t) => t.path)).not.toContain('z.mp3');
  });

  it('refuses a key or a mood it cannot read', () => {
    const rows = readImageRows(image());
    expect(() => searchImage(rows, mediaRows(), { key: ['bogus'] })).toThrow(McpError);
    expect(() => searchImage(rows, mediaRows(), { mood: ['angry'] })).toThrow(McpError);
  });
});
