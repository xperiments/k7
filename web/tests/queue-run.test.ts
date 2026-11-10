/**
 * The run's transitions, without React, without a card and without a worker.
 *
 * Every case here was unreachable before `queue/run.ts` existed: the provider owns the state,
 * the pool and four stores, and the App's test environment is node, so `processItem` had no
 * test at all. These drive the road with fakes and read the patches it emits.
 */
import { describe, expect, it } from 'vitest';
import type { AnalysisClient } from '../src/audio/client';
import type { TrackMetadata } from '../src/audio/metadata';
import type { Analysis } from '../src/domain/analysis';
import { runItem, type RunDeps } from '../src/queue/run';
import type { QueueItem } from '../src/ui/queue';
import type { CachedMedia } from '../src/storage/media';
import { METADATA_WRITE_VERSION } from '../src/audio/tagWriter';
import { sha256Hex } from '../src/audio/decode';
import { WeightUnavailableError } from '../src/weights/errors';

const MODEL_SET = 'models@1';
const PATH = 'music/Artist/Album/Track.mp3';
/** The content hash of the fake bytes every case reads, which is what `writeCurrent` compares. */
const HASH = await sha256Hex(new Uint8Array(10));

function itemOf(size = 10, lastModified = 1000): QueueItem {
  return {
    id: 'row-1',
    file: new File([new Uint8Array(size)], 'Track.mp3', { lastModified }),
    name: 'Track',
    path: PATH,
    format: 'mp3',
    status: 'queued',
  };
}

function mediaOf(over: Partial<CachedMedia> = {}): CachedMedia {
  return {
    path: 'Artist/Album/Track.mp3',
    title: 'Track',
    artist: 'Artist',
    album: 'Album',
    genre: 'House',
    at: '2026-01-01T00:00:00.000Z',
    size: 10,
    mtime: 1000,
    hash: 'hash-1',
    ...over,
  };
}

function analysisOf(hash = 'hash-1'): Analysis {
  return {
    track: { name: 'Track', path: PATH, size: 10, format: 'mp3', hash },
    bpm: 124,
  } as unknown as Analysis;
}

const TAGS: TrackMetadata = {
  tags: { title: 'Track', artist: 'Artist', album: 'Album', genre: 'House' },
  bpm: 124,
  key: { camelot: '8A' },
} as unknown as TrackMetadata;

const PLAIN_TAGS: TrackMetadata = {
  tags: { title: 'Track', artist: 'Artist', album: 'Album', genre: 'House' },
} as unknown as TrackMetadata;

interface Recorder {
  deps: RunDeps;
  patches: Array<{ id: string; patch: Partial<QueueItem> }>;
  calls: string[];
  statuses(): string[];
}

function depsOf(over: Partial<RunDeps> = {}, media: CachedMedia | undefined = undefined): Recorder {
  const patches: Array<{ id: string; patch: Partial<QueueItem> }> = [];
  const calls: string[] = [];
  const base: RunDeps = {
    modelSet: MODEL_SET,
    tagsOnly: true,
    update: (id, patch) => {
      patches.push({ id, patch });
    },
    read: async () => {
      calls.push('read');
      return { file: new File([new Uint8Array(10)], 'Track.mp3'), bytes: new Uint8Array(10) };
    },
    readTags: async () => {
      calls.push('readTags');
      return TAGS;
    },
    decode: async () => {
      calls.push('decode');
      return {} as never;
    },
    getCached: async () => {
      calls.push('getCached');
      return null;
    },
    putCached: async () => {
      calls.push('putCached');
    },
    getMedia: async () => {
      calls.push('getMedia');
      return media;
    },
    putMedia: async () => {
      calls.push('putMedia');
    },
    finalize: async () => {
      calls.push('finalize');
    },
    settled: () => {
      calls.push('settled');
    },
    weightError: () => {
      calls.push('weightError');
    },
    paused: () => {
      calls.push('paused');
    },
    unreadable: { current: 0 },
    released: () => {
      calls.push('released');
    },
  };
  const deps = { ...base, ...over };
  return {
    deps,
    patches,
    calls,
    statuses: () => patches.filter((p) => p.patch.status).map((p) => p.patch.status as string),
  };
}

const client = {
  analyze: async (_req: unknown, _audio: unknown, _tags: unknown, onStage?: (s: string) => void) => {
    onStage?.('tempo');
    return analysisOf();
  },
} as unknown as AnalysisClient;

describe('the run, from queued to settled', () => {
  it('takes the fast path when the row and the bytes still agree, and does not open the file', async () => {
    const rec = depsOf({ getCached: async () => analysisOf(HASH) },
      mediaOf({ hash: HASH, written: METADATA_WRITE_VERSION }));
    await runItem(itemOf(), client, rec.deps);

    expect(rec.statuses()).toEqual(['done']);
    expect(rec.patches[0]?.patch).toMatchObject({ cached: true });
    expect(rec.calls).toContain('getMedia');
    expect(rec.calls).not.toContain('read');
    expect(rec.calls).not.toContain('finalize');
    expect(rec.calls).toContain('released');
  });

  it('finalizes a settled row whose file was never written by this build', async () => {
    const rec = depsOf({ getCached: async () => analysisOf(HASH) },
      mediaOf({ hash: HASH, written: undefined }));
    await runItem(itemOf(), client, rec.deps);

    expect(rec.statuses()).toEqual(['done']);
    expect(rec.calls).toContain('finalize');
    expect(rec.calls).not.toContain('read');
  });

  it('builds a tag-only row when the tags already carry tempo and key', async () => {
    const rec = depsOf({ getMedia: async () => undefined });
    await runItem(itemOf(), client, rec.deps);

    expect(rec.statuses()).toEqual(['hashing', 'decoding', 'tagged']);
    expect(rec.calls).not.toContain('decode');
    expect(rec.calls).toContain('putCached');
    expect(rec.calls).toContain('settled');
    expect(rec.calls).toContain('finalize');
  });

  it('measures when the tags do not carry tempo and key, and reports the stage', async () => {
    const rec = depsOf({
      getMedia: async () => undefined,
      readTags: async () => PLAIN_TAGS,
    });
    await runItem(itemOf(), client, rec.deps);

    expect(rec.statuses()).toEqual(['hashing', 'decoding', 'analysing', 'done']);
    expect(rec.calls).toContain('decode');
    expect(rec.patches.some((p) => p.patch.stage === 'tempo')).toBe(true);
    expect(rec.calls).toContain('finalize');
  });

  it('takes the cache after hashing, and refreshes a row whose size or time moved', async () => {
    const rec = depsOf({
      getMedia: async () => mediaOf({ hash: HASH, size: 99, written: METADATA_WRITE_VERSION }),
      getCached: async () => analysisOf(HASH),
    });
    await runItem(itemOf(), client, rec.deps);

    expect(rec.statuses()).toEqual(['hashing', 'decoding', 'done']);
    expect(rec.calls).toContain('putMedia');
    expect(rec.calls).not.toContain('finalize');
    expect(rec.calls).not.toContain('decode');
  });

  it('pauses the run on the third dead snapshot, not on the first', async () => {
    const unreadable = { current: 0 };
    const rec = depsOf({
      getMedia: async () => undefined,
      read: async () => {
        throw new DOMException('the requested file could not be read', 'NotReadableError');
      },
      unreadable,
    });

    await runItem(itemOf(), client, rec.deps);
    expect(rec.calls).not.toContain('paused');
    await runItem({ ...itemOf(), id: 'row-2' }, client, rec.deps);
    expect(rec.calls).not.toContain('paused');
    await runItem({ ...itemOf(), id: 'row-3' }, client, rec.deps);

    expect(rec.calls).toContain('paused');
    expect(unreadable.current).toBe(3);
    expect(rec.statuses().filter((s) => s === 'error')).toHaveLength(3);
  });

  it('names a weight failure and still fails the row', async () => {
    const rec = depsOf({
      getMedia: async () => undefined,
      readTags: async () => {
        throw new WeightUnavailableError({ label: 'the model', sha256: 'x', url: 'u' } as never);
      },
    });
    await runItem(itemOf(), client, rec.deps);

    expect(rec.calls).toContain('weightError');
    expect(rec.statuses().at(-1)).toBe('error');
    expect(rec.calls).toContain('released');
  });
});
