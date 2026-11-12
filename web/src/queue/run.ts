/**
 * One Track's road from `queued` to settled.
 *
 * This was the body of `AnalysisProvider`'s `processItem`, which is where it could not be
 * tested: the provider owns React state, the worker pool, four IndexedDB stores and the
 * sentences a person reads, and a test cannot mount it — the App's test environment is node,
 * with no DOM and no `.test.tsx` anywhere. The decisions were therefore the least covered and
 * the most consequential part of the App: a wrong transition shows up as a row that never
 * settles, which is exactly what nobody can reproduce on demand.
 *
 * So the road is here, and everything it touches arrives as a port. What is *not* a port is
 * the pure part: which row counts as settled, when the file's own tags are enough, what the
 * cache key is, how a tag-only row is built. Those already live in tested modules and are
 * called directly, which is what makes this module's behaviour readable in one place.
 *
 * The rule for the ports is the one the App's other seams follow: a dependency this module
 * creates is a dependency a test cannot replace. `decode` in particular has to be a port,
 * because decoding a file means an `AudioContext`, and a test has neither.
 */
import type { AnalysisClient } from '../audio/client';
import type { TrackMetadata } from '../audio/metadata';
import { sha256Hex, type decodeFile } from '../audio/decode';
import type { Analysis } from '../domain/analysis';
import { snapshotPath } from '../domain/snapshot';
import { needsMeasurement, tagAnalysis } from '../domain/tagAnalysis';
import { isNotReadable } from '../card/readable';
import { cacheKey } from '../config';
import { measuredHash, tagsOf, writeCurrent, type CachedMedia } from '../storage/media';
import { METADATA_WRITE_VERSION } from '../audio/tagWriter';
import { isWeightError } from '../weights/errors';
import type { QueueItem } from '../ui/queue';

/** What the App can decode a file into; the analyser's input. */
export type DecodedAudio = Awaited<ReturnType<typeof decodeFile>>;

/** The row this Track already has, when the card still holds the same bytes. */
export interface KnownRow {
  analysis: Analysis;
  tags: ReturnType<typeof tagsOf>;
  media: CachedMedia;
}

export interface RunDeps {
  /** The model set the cache is keyed by. The caller asks once, before any lane starts. */
  readonly modelSet: string;
  /** Use the file's own tags and measure only the Tracks they do not cover. */
  readonly tagsOnly: boolean;

  /** One patch per transition. The caller applies it to whatever holds the queue. */
  update(id: string, patch: Partial<QueueItem>): void;

  /**
   * The bytes, and the file they came from. The queue's `File` is the scan's snapshot and a
   * long run outlives it, so this reopens the card's file when the browser says the snapshot
   * is dead; every later read of this Track uses the file these bytes came from.
   */
  read(item: QueueItem): Promise<{ file: File; bytes: Uint8Array }>;
  readTags(file: File): Promise<TrackMetadata>;
  decode(buffer: ArrayBuffer, need44k: boolean): Promise<DecodedAudio>;

  getCached(key: string): Promise<Analysis | null>;
  putCached(key: string, analysis: Analysis): Promise<void>;
  getMedia(path: string): Promise<CachedMedia | undefined>;
  putMedia(row: CachedMedia): Promise<void>;

  /** Write the file's own tags and label, and re-key the analysis to its new bytes. */
  finalize(
    item: QueueItem,
    client: AnalysisClient,
    analysis: Analysis,
    hash: string,
    media?: CachedMedia,
  ): Promise<void>;

  /** The first analysis is when persistence is worth asking the browser for, once a page. */
  settled(): void;
  /** A weight could not be produced: the analyser cannot start, and the UI says why. */
  weightError(message: string): void;
  /** Too many dead snapshots in a row means the card is gone, not that a Track is bad. */
  paused(): void;

  /**
   * Consecutive unreadable snapshots, kept across Tracks because it is the card's state, not
   * a Track's. The caller owns it so that a pause outlives the lane that noticed.
   */
  readonly unreadable: { current: number };

  /** The scheduler's claim on this row is over; another lane may take it. */
  released(id: string): void;
}

/** The row this Track would have had, if the card still holds the same bytes. */
async function knownRow(item: QueueItem, deps: RunDeps): Promise<KnownRow | null> {
  const path = snapshotPath(item.path);
  if (path === null) return null;
  const media = await deps.getMedia(path).catch(() => undefined);
  if (media === undefined) return null;
  const hash = measuredHash(media, item.file);
  if (hash === null) return null;
  const cached = await deps.getCached(cacheKey(hash, deps.modelSet)).catch(() => null);
  if (!cached) return null;
  return {
    analysis: {
      ...cached,
      track: { ...cached.track, name: item.name, path: item.path, size: item.file.size, hash },
    },
    // The row was never opened, so its tags come from the record, not from the file.
    tags: tagsOf(media),
    media,
  };
}

/**
 * Settle one Track: the cache, then the file's own tags, then the measurements the tags do not
 * cover. Every exit either settles the row or names why it did not.
 */
export async function runItem(
  item: QueueItem,
  client: AnalysisClient,
  deps: RunDeps,
): Promise<void> {
  const { modelSet, update } = deps;
  try {
    /* Already measured, and the card still holds the same bytes: nothing to read. */
    const known = await knownRow(item, deps);
    if (known !== null) {
      update(item.id, {
        status: 'done',
        analysis: known.analysis,
        tags: known.tags,
        cached: true,
      });
      /* The row and the file are both settled: a finalize recorded exactly this writer after
       * it last ran, so there is nothing to open, draw or write. Without that record (an
       * older row, or a card that was read-only) the file may still need its tags and label,
       * so the lane does that work once. */
      if (!writeCurrent(known.media, known.analysis.track.hash, METADATA_WRITE_VERSION)) {
        await deps.finalize(item, client, known.analysis, known.analysis.track.hash, known.media);
      }
      return;
    }

    update(item.id, { status: 'hashing', error: undefined });
    const { file: source, bytes } = await deps.read(item);
    const buffer = bytes.buffer as ArrayBuffer;
    const hash = await sha256Hex(bytes);
    const key = cacheKey(hash, modelSet);

    update(item.id, { status: 'decoding' });

    /* The file's own tags are read before the cache is consulted. They are not part of a
     * stored analysis — the Prepare row shows the genre, and a cached Track would otherwise
     * be the one row without it — and the Snapshot export reuses them instead of re-reading
     * the file. */
    const metadata = await deps.readTags(source);
    update(item.id, { tags: metadata.tags });

    const cached = await deps.getCached(key);
    if (cached) {
      // Identity is the content hash; the file may have been found at a different path.
      const analysis: Analysis = {
        ...cached,
        track: { ...cached.track, name: item.name, path: item.path, size: source.size, hash },
      };
      update(item.id, { status: 'done', analysis, cached: true });
      /* The analysis is keyed by the content's hash, not by the card: this file may be known
       * from another card while this card's shelf has never seen it. The row is what Media
       * joins on, so measuring it once here is what keeps "processed" from being reported as
       * "not measured".
       *
       * A row that already records this exact file and this build's finalize is settled even
       * though the fast path above missed it (its mtime is not the one the row kept);
       * refreshing the record is enough, and the next load takes the fast path. */
      const path = snapshotPath(item.path);
      const media =
        path === null ? undefined : await deps.getMedia(path).catch(() => undefined);
      if (writeCurrent(media, hash, METADATA_WRITE_VERSION)) {
        if (
          media !== undefined &&
          (media.size !== source.size || media.mtime !== source.lastModified)
        ) {
          await deps.putMedia({
            ...media,
            size: source.size,
            mtime: source.lastModified,
            at: new Date().toISOString(),
          });
        }
        return;
      }
      await deps.finalize({ ...item, file: source }, client, analysis, hash);
      return;
    }

    /* A Track whose tags already carry BPM and key needs no inference: the row is built from
     * the tags and the fields they cannot carry stay unknown. */
    if (!needsMeasurement(metadata, deps.tagsOnly)) {
      const analysis = tagAnalysis(
        {
          name: source.name,
          path: item.path,
          size: source.size,
          format: item.format,
          hash,
        },
        metadata,
      );
      await deps.putCached(key, analysis);
      deps.settled();
      update(item.id, { status: 'tagged', analysis, stage: undefined });
      await deps.finalize({ ...item, file: source }, client, analysis, hash);
      return;
    }

    const need44k = metadata.bpm === undefined || metadata.key === undefined;
    const audio = await deps.decode(buffer, need44k);
    update(item.id, { status: 'analysing', stage: undefined });
    const analysis = await client.analyze(
      {
        name: source.name,
        path: item.path,
        size: source.size,
        format: item.format,
        hash,
      },
      audio,
      metadata,
      (stage) => update(item.id, { stage }),
    );
    await deps.putCached(key, analysis);
    deps.settled();
    update(item.id, { status: 'done', analysis, stage: undefined });
    await deps.finalize({ ...item, file: source }, client, analysis, hash);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    if (isWeightError(error)) deps.weightError(message);
    /* A snapshot the card would not hand back means the run has lost the card, not that one
     * Track is bad. Three in a row is the card, and the run pauses once instead of painting
     * every remaining row as an error. */
    if (isNotReadable(error)) {
      deps.unreadable.current += 1;
      if (deps.unreadable.current >= 3) deps.paused();
    } else {
      deps.unreadable.current = 0;
    }
    update(item.id, { status: 'error', error: message });
  } finally {
    deps.released(item.id);
  }
}
