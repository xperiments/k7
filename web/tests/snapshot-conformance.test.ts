import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { ANALYSIS_SCHEMA, type Analysis, type MoodTag } from '../src/domain/analysis';
import { buildSnapshot, type SnapshotTrack } from '../src/domain/snapshot';
import type { SearchArgs } from '../src/device/mcp';
import { readImageRows, searchImage } from '../src/library/imageSource';
import type { CachedMedia } from '../src/storage/media';

/**
 * The S4 conformance corpus: one JSON file, read here and by `host/tests/t29_snapshot.c`.
 * The two implementations share the fixture, not the code; if either drifts on a filter or
 * a sort, one of the two suites fails. The rows are the App's own producer input, so the
 * C side also proves the bytes this builder writes.
 */
interface CorpusRow {
  path: string;
  size: number;
  duration_ms?: number;
  bpm?: number;
  key?: string;
  energy?: number;
  danceability?: number;
  valence?: number;
  arousal?: number;
  confidence?: number;
  mood?: string[];
  genre?: string;
  title?: string;
  artist?: string;
  album?: string;
}

interface CorpusCase {
  name: string;
  args: Record<string, number | string>;
  /** How many tracks matched before the page; defaults to the page's length. */
  matched?: number;
  expect: string[];
}

interface Corpus {
  tracks: CorpusRow[];
  cases: CorpusCase[];
}

const corpus = JSON.parse(
  readFileSync(new URL('../../host/tests/snapshot-conformance.json', import.meta.url), 'utf8'),
) as Corpus;

const MOODS: readonly Exclude<MoodTag, 'danceable'>[] = [
  'happy',
  'sad',
  'aggressive',
  'relaxed',
  'party',
];

function analysisOf(row: CorpusRow): Analysis {
  const hasTags = row.danceability !== undefined || (row.mood?.length ?? 0) > 0;
  const tags = hasTags
    ? ({
        happy: row.mood?.includes('happy') ? 1 : 0,
        sad: row.mood?.includes('sad') ? 1 : 0,
        aggressive: row.mood?.includes('aggressive') ? 1 : 0,
        relaxed: row.mood?.includes('relaxed') ? 1 : 0,
        party: row.mood?.includes('party') ? 1 : 0,
        danceable: (row.danceability ?? 0) / 100,
      } satisfies Record<MoodTag, number>)
    : null;
  return {
    schema: ANALYSIS_SCHEMA,
    track: {
      name: row.path.split('/').pop() ?? row.path,
      path: row.path,
      size: row.size,
      format: row.path.endsWith('.flac') ? 'flac' : 'mp3',
      duration: (row.duration_ms ?? 0) / 1000,
      hash: 'h',
    },
    tempo: {
      bpm: row.bpm ?? 0,
      rawBpm: row.bpm ?? 0,
      confidence: row.confidence === undefined ? null : row.confidence / 100,
      engine: 'fast',
      escalated: false,
      suggestEscalation: false,
      source: 'measured',
    },
    key: {
      tonic: 'A',
      mode: 'minor',
      camelot: row.key ?? '',
      strength: 0.5,
      source: 'measured',
    },
    mood: {
      tags,
      label: null,
      source: 'measured',
      valence: row.valence === undefined ? null : row.valence / 100,
      arousal: row.arousal === undefined ? null : row.arousal / 100,
    },
    energy: row.energy === undefined ? null : row.energy / 100,
    energySource: row.energy === undefined ? null : 'measured',
    provenance: {
      analyzer: 'conformance',
      modelSet: 'm',
      window: null,
      analysedAt: '2026-01-01T00:00:00Z',
    },
  };
}

function argsOf(raw: CorpusCase['args']): SearchArgs {
  const args: SearchArgs = {};
  if (typeof raw.query === 'string') args.query = raw.query;
  if (typeof raw.genre === 'string') args.genre = raw.genre;
  if (typeof raw.bpm_min === 'number') args.bpmMin = raw.bpm_min;
  if (typeof raw.bpm_max === 'number') args.bpmMax = raw.bpm_max;
  if (typeof raw.duration_min === 'number') args.durationMin = raw.duration_min;
  if (typeof raw.duration_max === 'number') args.durationMax = raw.duration_max;
  if (typeof raw.sort === 'string') args.sort = raw.sort as SearchArgs['sort'];
  if (typeof raw.offset === 'number') args.offset = raw.offset;
  if (typeof raw.limit === 'number') args.limit = raw.limit;
  return args;
}

function imageSource(): { rows: ReturnType<typeof readImageRows>; media: CachedMedia[] } {
  const tracks: SnapshotTrack[] = corpus.tracks.map((row) => ({
    path: `CARD/music/${row.path}`,
    size: row.size,
    title: row.title ?? '',
    artist: row.artist ?? '',
    album: row.album ?? '',
    genre: row.genre ?? '',
    analysis: analysisOf(row),
  }));
  const bytes = buildSnapshot({
    tracks,
    generationSeconds: 1,
    analyserVersion: 300,
    appSchema: 1,
    modelSet: 1n,
  });
  const media: CachedMedia[] = corpus.tracks.map((row) => ({
    path: row.path,
    title: row.title ?? '',
    artist: row.artist ?? '',
    album: row.album ?? '',
    genre: row.genre ?? '',
    at: '2026-01-01T00:00:00Z',
    size: row.size,
  }));
  return { rows: readImageRows(bytes), media };
}

describe('the S4 conformance corpus', () => {
  const { rows, media } = imageSource();

  for (const testCase of corpus.cases) {
    it(testCase.name, () => {
      const answer = searchImage(rows, media, argsOf(testCase.args));
      expect(answer.tracks.map((track) => track.path)).toEqual(testCase.expect);
      expect(answer.matched).toBe(testCase.matched ?? testCase.expect.length);
    });
  }

  it('carries each track\'s length in the device\'s JSON shape', () => {
    const answer = searchImage(rows, media, { sort: 'duration' });
    expect(answer.tracks[0]?.analysis?.durationMs).toBe(60000);
    expect(answer.tracks.at(-1)?.analysis?.durationMs).toBeNull();
  });
});

// The corpus says "this is what a length means"; it must stay a corpus, not drift into
// whatever the builder happened to write today.
describe('the corpus fixture', () => {
  it('has a measured, an unknown and a tied length', () => {
    const lengths = corpus.tracks.map((row) => row.duration_ms ?? 0);
    expect(lengths).toContain(0);
    expect(new Set(lengths).size).toBeGreaterThan(1);
    expect(corpus.tracks.every((row) => typeof row.size === 'number')).toBe(true);
  });

  it('names every mood it uses', () => {
    for (const row of corpus.tracks) {
      for (const mood of row.mood ?? []) expect(MOODS).toContain(mood);
    }
  });
});
