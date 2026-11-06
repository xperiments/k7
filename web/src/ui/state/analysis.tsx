import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useRef,
  useState,
  type ReactNode,
} from 'react';
import { AnalysisClient } from '../../audio/client';
import type { FinalizeResult } from '../../audio/protocol';
import { startWorkerPool, type WorkerCapacity } from '../../audio/workers';
import { decodeFile, sha256Hex, trackFormat } from '../../audio/decode';
import { loadManifest } from '../../audio/models';
import { readTrackMetadata, type TrackTags } from '../../audio/metadata';
import { downloadTaggedCopy, METADATA_WRITE_VERSION, writeAnalysisTags, writeTrackLabel } from '../../audio/tagWriter';
import { coverSource, type EmbeddablePicture } from '../../audio/covers';
import type { Analysis, ModelManifest } from '../../domain/analysis';
import { snapshotPath } from '../../domain/snapshot';
import { needsMeasurement, tagAnalysis } from '../../domain/tagAnalysis';
import { newId } from '../../lib/id';
import { toSnapshotPlaylists, useDrafts } from '../../playlists/drafts';
import { patchListArea } from '../../playlists/image';
import { countMedia, getMedia, listMedia, measuredHash, putMedia, tagsOf, writeCurrent, type CachedMedia } from '../../storage/media';
import { cardRefFor, currentCardScope, subscribeCardScope } from '../../card/session';
import { readCardFile as readFileFromCard, type CardRoot } from '../../card/read';
import { isNotReadable, openReadable, withReadable } from '../../card/readable';
import { writableCard, writeCardFile } from '../../card/write';
import {
  cardHandleFor,
  ensureAccess,
  ensureRead,
  forgetCardHandle,
} from '../../card/handles';
import { loadSnapshot, saveSnapshot, type StoredSnapshot } from '../../storage/snapshots';
import { clearCache, getCached, putCached } from '../../storage/cache';
import { requestPersistence } from '../../storage/persist';

/** O5: the first analysis is when persistence is worth asking the browser for. */
let persistenceAsked = false;
function askPersistenceOnce(): void {
  if (persistenceAsked) return;
  persistenceAsked = true;
  void requestPersistence();
}
import { cacheKey } from '../../config';
import { getSettings } from '../../settings';
import { isWeightError } from '../../weights/errors';
import { writeSnapshotToCard, type SnapshotDirectory } from '../cardWriter';
import { readCardWithPicker, readOpenCard } from '../intake';
import type { FoundFile } from '../files';
import { plural } from '../format';
import type { QueueItem } from '../queue';
import { buildSnapshotExport, type SnapshotExport } from '../snapshotExport';
import { runStage } from '../stage';

/** A result the user should read: a tone for the alert, and what to say. */
export interface Notice {
  kind: 'success' | 'error';
  text: string;
}

/**
 * What the Card area measures and what it produces, in one place.
 *
 * This is the old PrepareView's state and actions, lifted out of the page and mounted
 * once at the app root. The reason is ADR 0052's: the Process area stays mounted while
 * another route is shown, so navigating away can never destroy a queue that is still
 * working. Keeping the state inside the area would have made that promise a coincidence
 * of `hidden` styling rather than a property of the app.
 */
export interface AnalysisApi {
  /** The queue, in submission order. */
  items: QueueItem[];
  /** True while any worker holds a row. */
  busy: boolean;
  /** The boot check's verdict on the worker pool; null until it has run. */
  workers: WorkerCapacity | null;
  /**
   * Whether queued rows are being handed to the workers. True unless **Pause** was pressed:
   * a new queue always measures, and Pause holds the run it was pressed on until Resume.
   */
  running: boolean;
  pause: () => void;
  resume: () => void;
  /** What the run is doing, for the header; null when nothing is in flight. */
  busyStage: string | null;
  /** Rows with an analysis, and rows in total. */
  done: number;
  total: number;

  /** The model set the cached analyses are keyed by; null until the weights load. */
  modelSet: string | null;
  /** The weights did not load: the analyser cannot start, and says why. */
  setupError: string | null;
  /** A weight was refused during a run. */
  weightError: string | null;
  notice: Notice | null;
  dismissNotice: () => void;
  dismissWeightError: () => void;

  /** Covers and tags kept in this browser for the library views. */
  mediaCount: number;
  /**
   * Tracks whose measured tags and label are still being written into their file. The run is
   * not over while this is above zero - the writes move sizes the image reports - and the
   * page says so rather than leaving a full bar to mean "done".
   */
  metadataPending: number;
  /** The image this browser built (and the player accepted, once `sentAt` is set). */
  lastSnapshot: StoredSnapshot | null;
  /** An image is being built, written or sent. */
  saving: boolean;

  /** Use the file's own tags and measure only the tracks they do not cover. */
  tagsOnly: boolean;
  setTagsOnly: (value: boolean) => void;

  /** The in-place "clear the queue?" confirmation. */
  confirmingClear: boolean;
  setConfirmingClear: (value: boolean) => void;

  addFound: (found: FoundFile[], skipped: number, readOnly?: boolean) => void;
  addCard: (found: FoundFile[], skipped: number, handle: SnapshotDirectory) => void;
  /** Walk the card this page holds open again; its files replace the queue. */
  rescanCard: () => Promise<void>;
  /** Open a different card; its files replace the queue and the Scope follows. */
  changeCard: () => Promise<void>;
  /** The card was opened read-write in this session, so it can be written back to. */
  hasCardHandle: boolean;
  /**
   * A card this browser can read: the one opened this visit, or the one it remembers opening
   * before. Rescan only needs read access, so it outlives the write grant.
   */
  canRescan: boolean;
  /**
   * The bytes of one track as the card holds them now, by its path below `music` — the card
   * handle when this page has one, the queue's own file when it does not. `null` means the
   * card is not here: nothing can be played from it.
   */
  readCardFile: (path: string) => Promise<File | null>;

  retry: (item: QueueItem) => void;
  deepAnalyse: (item: QueueItem) => Promise<void>;
  saveTags: (item: QueueItem) => Promise<void>;

  /** When the card last took an image from this page, or null if it has not. */
  writtenAt: string | null;
  /** The card this page is working on, as its scope names it: a mark, or a fingerprint. */
  cardLabel: string;
  clearAll: () => Promise<void>;
}

const AnalysisContext = createContext<AnalysisApi | null>(null);

export function AnalysisProvider({ children }: { children: ReactNode }) {
  const [items, setItems] = useState<QueueItem[]>([]);
  const [busy, setBusy] = useState(false);
  /* Every intake starts a run; Pause is the only thing that holds it. */
  const [running, setRunning] = useState(true);
  const [manifest, setManifest] = useState<ModelManifest | null>(null);
  const modelSet = manifest?.modelSet ?? null;
  const [saving, setSaving] = useState(false);
  /* Bumped whenever a Track's analysis changes, so the card-write effect can tell a new
   * measurement from the one it already wrote (a deep analysis keeps the content hash). */
  const [runRevision, setRunRevision] = useState(0);
  const [tagsOnly, setTagsOnly] = useState(true);
  const [lastSnapshot, setLastSnapshot] = useState<StoredSnapshot | null>(null);
  const [mediaCount, setMediaCount] = useState(0);
  const [confirmingClear, setConfirmingClear] = useState(false);
  /* The card the user opened read-write, when they opened one: the image goes back there
   * without asking again. A dropped folder leaves this null. */
  const [cardHandle, setCardHandle] = useState<SnapshotDirectory | null>(null);
  /* The same card's handle as this browser remembered it, when this visit has not opened one.
   * It is not a grant: reading needs the permission asked for again on the gesture. */
  const [storedHandle, setStoredHandle] = useState<FileSystemDirectoryHandle | null>(null);
  const [setupError, setSetupError] = useState<string | null>(null);
  const [weightError, setWeightError] = useState<string | null>(null);
  const [notice, setNotice] = useState<Notice | null>(null);

  /* The pool starts at boot. The setting is read once — auto (half the logical cores, capped
   * by memory) or a pinned count — the app starts that many workers, and keeps the ones that
   * answer a small probe. Each worker loads its own model sessions, which is the price of the
   * parallelism and the reason auto stays small. A changed setting applies from the next
   * reload; the Settings page says so. */
  const [clients, setClients] = useState<AnalysisClient[]>([]);
  const [workerCapacity, setWorkerCapacity] = useState<WorkerCapacity | null>(null);
  const clientsRef = useRef<AnalysisClient[]>([]);

  useEffect(() => {
    let cancelled = false;
    void startWorkerPool(getSettings().workers)
      .then((booted) => {
        if (cancelled) {
          for (const client of booted.clients) client.dispose();
          return;
        }
        clientsRef.current = booted.clients;
        setClients(booted.clients);
        setWorkerCapacity(booted.capacity);
      })
      .catch((error: unknown) => {
        if (cancelled) return;
        setNotice({
          kind: 'error',
          text: `The analysis workers could not start: ${
            error instanceof Error ? error.message : String(error)
          }`,
        });
      });
    return () => {
      cancelled = true;
      for (const client of clientsRef.current) client.dispose();
      clientsRef.current = [];
    };
  }, []);

  const inFlight = useRef(0);
  const claimed = useRef(new Set<string>());
  /* A client holds its Track until the worker has also written the tags and the label and the
   * page has saved the bytes. Only then does the scheduler give it another row. */
  const busyClients = useRef(new Set<AnalysisClient>());
  const [laneTick, setLaneTick] = useState(0);

  /* Drafts are read reactively so a Playlist added while the page is open makes "Send to
   * player" available without a reload: a list is part of what an image is. */
  const drafts = useDrafts();

  /* The last image survives a reload, which is what makes the player's restart cheap: the
   * page can lose its origin for a second and still have the run to send again. */
  useEffect(() => {
    let cancelled = false;
    /* Both reads are for one card, so both are re-run when the pick changes which one that
     * is. A Last image left from the card before must never be what a list change is patched
     * onto and posted. */
    const loadForCurrentCard = () => {
      const scope = currentCardScope();
      const stillCurrent = () => !cancelled && scope === currentCardScope();
      setLastSnapshot(null);
      setStoredHandle(null);
      cardHandleFor(scope)
        .then((handle) => {
          if (stillCurrent()) setStoredHandle(handle);
        })
        .catch(() => {});
      countMedia(scope)
        .then((count) => {
          if (stillCurrent()) setMediaCount(count);
        })
        .catch(() => {});
      loadSnapshot(scope)
        .then((stored) => {
          if (stillCurrent()) setLastSnapshot(stored ?? null);
        })
        .catch(() => {
          /* A store that cannot be read is not a reason to fail the page. */
        });
    };
    loadForCurrentCard();
    const unsubscribe = subscribeCardScope(loadForCurrentCard);
    return () => {
      cancelled = true;
      unsubscribe();
    };
  }, []);

  useEffect(() => {
    let cancelled = false;
    loadManifest()
      .then((loaded) => {
        if (!cancelled) setManifest(loaded);
      })
      .catch((error: unknown) => {
        if (!cancelled) {
          setSetupError(error instanceof Error ? error.message : String(error));
        }
      });
    return () => {
      cancelled = true;
    };
  }, []);

  const update = useCallback((id: string, patch: Partial<QueueItem>) => {
    setItems((previous) =>
      previous.map((item) => (item.id === id ? { ...item, ...patch } : item)),
    );
    if (patch.analysis !== undefined) setRunRevision((revision) => revision + 1);
  }, []);

  /* A long run outlives its File snapshots. Chrome hands back a File that records the bytes at
   * the moment `getFile()` was called, and a write to that file - this App's own tag write,
   * another tab, a sync tool, or the OS reclaiming an old snapshot - leaves the copy the queue
   * still holds unreadable: NotReadableError, once per file, for the rest of the run. The card
   * handle is still good, so the answer is to walk to the path again and take the file it has
   * now. A folder with no card path has no handle to reopen through; its snapshot is all there
   * is and the error is the caller's. */
  const reopenItem = useCallback(
    async (item: QueueItem): Promise<File | null> => {
      const path = snapshotPath(item.path);
      if (path === null) return null;
      let handle: SnapshotDirectory | null = cardHandle;
      if (handle === null && storedHandle !== null) {
        try {
          if (await ensureRead(storedHandle)) {
            handle = storedHandle as unknown as SnapshotDirectory;
          }
        } catch {
          /* No grant for the remembered handle: there is nothing to reopen through. */
        }
      }
      if (handle === null) return null;
      try {
        const fresh = await readFileFromCard(handle as unknown as CardRoot, path);
        if (fresh === null) return null;
        update(item.id, { file: fresh });
        return fresh;
      } catch {
        return null;
      }
    },
    [cardHandle, storedHandle, update],
  );

  /* The card's answer after too many stale snapshots it could not hand back: the run pauses
   * with one sentence rather than painting every remaining row as an error. */
  const unreadable = useRef(0);

  /** The file's bytes, reopening the card's snapshot once if it is dead. */
  const readItem = useCallback(
    async (item: QueueItem) => {
      const opened = await openReadable(item.file, () => reopenItem(item));
      unreadable.current = 0;
      return opened;
    },
    [reopenItem],
  );

  /* The card is in the computer exactly once, and the browser is the only place a cover can
   * be kept: the player serves paths and measurements, never a picture. So every track that
   * settles leaves its tags and its art here, keyed by the Snapshot's own path, and the
   * library views join the two by that key.
   *
   * The pictures come back with the row: the same parse that keeps the art is what the label
   * is drawn from (`audio/covers.ts`), so a Track is read once for both. */
  const rememberMedia = useCallback(
    async (
      item: QueueItem,
      hash: string,
      written = false,
    ): Promise<{ row: CachedMedia; pictures: EmbeddablePicture[] } | null> => {
      const path = snapshotPath(item.path);
      if (!path) return null;
      try {
        const { parseBlob } = await import('music-metadata');
        const { parsed, file: source } = await withReadable(
          item.file,
          () => reopenItem(item),
          async (file) => ({
            parsed: await parseBlob(file, { duration: false, skipCovers: false }),
            file,
          }),
        );
        const pictures: EmbeddablePicture[] = (parsed.common.picture ?? []).map((picture) => ({
          description: picture.description,
          type: picture.type,
          mimeType: picture.format,
          data: picture.data,
        }));
        // The Track's own front cover, never the worn label this App adds.
        const cover = coverSource(pictures) ?? pictures[0];
        const row: CachedMedia = {
          path,
          title: parsed.common.title ?? '',
          artist: parsed.common.artist ?? '',
          album: parsed.common.album ?? '',
          genre: parsed.common.genre?.[0] ?? '',
          cover: cover
            ? { bytes: new Uint8Array(cover.data), format: cover.mimeType ?? 'image/jpeg' }
            : undefined,
          at: new Date().toISOString(),
          size: source.size,
          mtime: source.lastModified,
          hash,
          /* Only a finalize that finished says the file carries this build's tags and label;
           * a row remembered without one is measured, not settled. */
          written: written ? METADATA_WRITE_VERSION : undefined,
        };
        await putMedia(currentCardScope(), row);
        void countMedia(currentCardScope()).then((n) => setMediaCount(n));
        return { row, pictures };
      } catch {
        /* Tags and art are a convenience: a file without them is measured like any other. */
        return null;
      }
    },
    [reopenItem],
  );

  /* The image waits for every tag/label write: each changes a Track's size and hash, and the
   * Snapshot carries that size. With the writers in the workers this is at most one per lane,
   * not a queue of hundreds. */
  const [metadataPending, setMetadataPending] = useState(0);

  /**
   * Finish one Track in the lane that measured it.
   *
   * The worker writes the tags and the label and hands back the bytes; the page writes those
   * bytes to the card once. Only a card this page opened read-write is touched, and only for a
   * file the run read from it — a dropped folder has no handle to write through. The caller
   * awaits this before releasing the worker, so a fast analyser cannot outrun the writer.
   *
   * The file's size and hash move with the tags, so the row, the analysis and the cache are
   * re-keyed to the bytes the card now holds; otherwise the next pass would decide every
   * written Track had changed and measure the library again.
   */
  const finalizeTrack = useCallback(
    async (
      item: QueueItem,
      client: AnalysisClient,
      analysis: Analysis,
      hash: string,
      known?: CachedMedia,
    ): Promise<void> => {
      const root = cardHandle?.name;
      const writable =
        root !== undefined && item.path.toLowerCase().startsWith(`${root.toLowerCase()}/`);

      if (!writable) {
        /* A dropped folder has no handle to write through; the row is still remembered so
         * Media can show it, and the row menu can save a tagged copy. */
        if (known === undefined) await rememberMedia(item, hash);
        return;
      }

      setMetadataPending((count) => count + 1);
      try {
        let file = item.file;
        let fileHash = hash;
        let current = analysis;

        let finalized: FinalizeResult | null = null;
        try {
          finalized = await client.finalize(
            item.file,
            analysis,
            cardHandle as unknown as FileSystemDirectoryHandle,
            item.path,
          );
        } catch (error) {
          /* The worker's first read is the file the queue holds. If that snapshot is dead,
           * the card can still hand the file back: give the lane the fresh one and retry. */
          if (isNotReadable(error)) {
            const fresh = await reopenItem(item);
            if (fresh !== null) {
              /* The try below may return no change, so keep the file the card handed back:
               * the media row reads this one, not the dead snapshot. */
              file = fresh;
              finalized = await client
                .finalize(
                  fresh,
                  analysis,
                  cardHandle as unknown as FileSystemDirectoryHandle,
                  item.path,
                )
                .catch(() => null);
            }
          }
        }
        let settled = finalized?.kind === 'complete';
        if (finalized?.kind === 'written') {
          let fresh: File | null = 'file' in finalized ? finalized.file : null;
          if (fresh === null && 'bytes' in finalized && cardHandle !== null) {
            /* The worker could not write through the cloned handle; this page still can. */
            const card = writableCard(cardHandle);
            if (card !== null) {
              fresh = await writeCardFile(card, item.path, finalized.bytes).catch(() => null);
            }
          }
          if (fresh !== null) {
            file = fresh;
            fileHash = finalized.hash;
            current = {
              ...analysis,
              track: { ...analysis.track, size: fresh.size, hash: fileHash },
            };
            settled = true;
            if (modelSet !== null) {
              await putCached(cacheKey(fileHash, modelSet), current);
            }
            update(item.id, { analysis: current, file });
          }
        }

        if (known === undefined) {
          await rememberMedia({ ...item, file }, fileHash, settled);
        } else {
          /* A known row still records the finalize it just took (or keeps the one it had when
           * the write failed), so the next load can skip opening the file at all. */
          await putMedia(currentCardScope(), {
            ...known,
            size: file.size,
            mtime: file.lastModified,
            hash: fileHash,
            written: settled ? METADATA_WRITE_VERSION : known.written,
            at: new Date().toISOString(),
          });
        }
      } catch {
        /* A card that refuses the write is still measured; the tags only make a later pass
         * quicker. */
      } finally {
        setMetadataPending((count) => Math.max(0, count - 1));
      }
    },
    [cardHandle, modelSet, rememberMedia, update, reopenItem],
  );

  /**
   * The analysis *and the row's tags* for a file this browser has already measured, or null.
   *
   * The card's own record decides it: the media row stores the file's path, size,
   * modification time and content hash, so a re-scan can name the Track without reading a
   * byte. A file whose size or mtime moved is a different file and is measured again; a row
   * from before those fields existed has neither, and is measured once more rather than
   * guessed at.
   */
  const knownAnalysis = useCallback(
    async (
      item: QueueItem,
    ): Promise<{ analysis: Analysis; tags: TrackTags; media: CachedMedia } | null> => {
      if (!modelSet) return null;
      const path = snapshotPath(item.path);
      if (path === null) return null;
      const media = await getMedia(currentCardScope(), path).catch(() => undefined);
      const hash = measuredHash(media, item.file);
      if (hash === null || media === undefined) return null;
      const cached = await getCached(cacheKey(hash, modelSet)).catch(() => null);
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
    },
    [modelSet],
  );

  const processItem = useCallback(
    async (item: QueueItem, client: AnalysisClient) => {
      if (!modelSet) return;
      try {
        /* Already measured, and the card still holds the same bytes: nothing to read. */
        const known = await knownAnalysis(item);
        if (known !== null) {
          update(item.id, {
            status: 'done',
            analysis: known.analysis,
            tags: known.tags,
            cached: true,
          });
          /* The row and the file are both settled: a finalize recorded exactly this writer
           * after it last ran, so there is nothing to open, draw or write. Without that
           * record (an older row, or a card that was read-only) the file may still need its
           * tags and label, so the lane does that work once. */
          if (!writeCurrent(known.media, known.analysis.track.hash, METADATA_WRITE_VERSION)) {
            await finalizeTrack(
              item,
              client,
              known.analysis,
              known.analysis.track.hash,
              known.media,
            );
          }
          return;
        }
        update(item.id, { status: 'hashing', error: undefined });
        /* The queue's File is the scan's snapshot. A long run outlives it (the App's own tag
         * writes included), so this read reopens the card's file when the browser says the
         * snapshot is dead, and every later read uses the file those bytes came from. */
        const { file: source, bytes } = await readItem(item);
        const buffer = bytes.buffer;
        const hash = await sha256Hex(bytes);
        const key = cacheKey(hash, modelSet);

        update(item.id, { status: 'decoding' });

        /* The file's own tags are read before the cache is consulted. They are not part of a
         * stored analysis — the Process row shows the genre, and a cached Track would
         * otherwise be the one row without it — and the Snapshot export reuses them instead
         * of re-reading the file. */
        const metadata = await readTrackMetadata(source);
        update(item.id, { tags: metadata.tags });

        const cached = await getCached(key);
        if (cached) {
          // Identity is the content hash; the file may have been found at a different path.
          const analysis: Analysis = {
            ...cached,
            track: {
              ...cached.track,
              name: item.name,
              path: item.path,
              size: source.size,
              hash,
            },
          };
          update(item.id, { status: 'done', analysis, cached: true });
          /* The analysis is keyed by the content's hash, not by the card: this file may be
           * known from another card while this card's shelf has never seen it. The row is
           * what Media joins on, so measuring it once here is what keeps "processed" from
           * being reported as "not measured".
           *
           * A row that already records this exact file and this build's finalize is settled
           * even though the fast path above missed it (its mtime is not the one the row
           * kept); refreshing the record is enough, and the next load takes the fast path. */
          const path = snapshotPath(item.path);
          const media =
            path === null
              ? undefined
              : await getMedia(currentCardScope(), path).catch(() => undefined);
          if (writeCurrent(media, hash, METADATA_WRITE_VERSION)) {
            if (
              media !== undefined &&
              (media.size !== source.size || media.mtime !== source.lastModified)
            ) {
              await putMedia(currentCardScope(), {
                ...media,
                size: source.size,
                mtime: source.lastModified,
                at: new Date().toISOString(),
              });
            }
            return;
          }
          await finalizeTrack({ ...item, file: source }, client, analysis, hash);
          return;
        }

        /* A track whose tags already carry BPM and key needs no inference: the row is built
         * from the tags and the fields they cannot carry stay unknown. */
        if (!needsMeasurement(metadata, tagsOnly)) {
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
          await putCached(key, analysis);
        askPersistenceOnce();
          update(item.id, { status: 'tagged', analysis, stage: undefined });
          await finalizeTrack({ ...item, file: source }, client, analysis, hash);
          return;
        }

        const need44k = metadata.bpm === undefined || metadata.key === undefined;
        const audio = await decodeFile(buffer, need44k);
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
        await putCached(key, analysis);
        askPersistenceOnce();
        update(item.id, { status: 'done', analysis, stage: undefined });
        await finalizeTrack({ ...item, file: source }, client, analysis, hash);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (isWeightError(error)) setWeightError(message);
        /* A snapshot the card would not hand back means the run has lost the card, not that
         * one track is bad. Three in a row is the card, and the run pauses once instead of
         * painting every remaining row as an error. */
        if (isNotReadable(error)) {
          unreadable.current += 1;
          if (unreadable.current >= 3) {
            setRunning(false);
            setNotice({
              kind: 'error',
              text: 'The card stopped handing back its files. Rescan the card, then press Resume; the run is paused where it stopped.',
            });
          }
        } else {
          unreadable.current = 0;
        }
        update(item.id, { status: 'error', error: message });
      } finally {
        claimed.current.delete(item.id);
      }
    },
    [modelSet, update, tagsOnly, finalizeTrack, knownAnalysis, readItem],
  );

  /* Keep every lane busy with one Track at a time. A client is free only when its last
   * `processItem` has finished — analysis, tags, label and the card write — so no lane starts
   * another row while its file is still being saved. `laneTick` makes the effect run when a
   * lane frees, not only when an item's status changes. */
  useEffect(() => {
    if (!modelSet || !running || clients.length === 0) return;
    for (;;) {
      if (inFlight.current >= clients.length) break;
      const client = clients.find((each) => !busyClients.current.has(each));
      if (!client) break;
      const next = items.find(
        (item) => item.status === 'queued' && !claimed.current.has(item.id),
      );
      if (!next) break;
      claimed.current.add(next.id);
      busyClients.current.add(client);
      inFlight.current += 1;
      setBusy(true);
      void processItem(next, client).finally(() => {
        busyClients.current.delete(client);
        inFlight.current -= 1;
        if (inFlight.current === 0) setBusy(false);
        setLaneTick((tick) => tick + 1);
      });
    }
  }, [items, modelSet, processItem, clients, running, laneTick]);

  /**
   * Take a card's files into the queue.
   *
   * `replace` is what a re-read of the same card means: the queue *is* that card, so its
   * rows are the card's rows. Measuring again is cheap — everything the browser already has
   * is recognised from the card's own record — and appending would double every row.
   */
  const acceptIntake = useCallback(
    (found: FoundFile[], skipped: number, replace: boolean, readOnly = false) => {
    const accepted: QueueItem[] = [];
    const rejected: string[] = [];

    for (const { file, path } of found) {
      const format = trackFormat(file);
      if (!format) {
        rejected.push(path);
        continue;
      }
      accepted.push({
        id: newId(),
        file,
        name: file.name,
        path,
        format,
        status: 'queued',
      });
    }

    if (accepted.length > 0) {
      setItems((previous) => (replace ? accepted : [...previous, ...accepted]));
      // A new queue always measures; a pause is for the run it was pressed on, not a latch.
      setRunning(true);
    }

    if (accepted.length === 0) {
      setNotice({
        kind: 'error',
        text:
          found.length === 0
            ? 'No MP3 or FLAC files found.'
            : `No MP3 or FLAC files in ${plural(found.length, 'file')}.`,
      });
      return;
    }

    const leftovers: string[] = [];
    // Not "non-audio": WAV is audio the device plays, and this App never opens it. Said as
    // what it is, because Media reports the same files apart rather than as unmeasured.
    if (rejected.length > 0)
      leftovers.push(`${plural(rejected.length, 'file')} this App cannot measure`);
    if (skipped > 0) leftovers.push(`${plural(skipped, 'item')} unreadable`);

    if (leftovers.length > 0 || readOnly) {
      const parts = [
        `${replace ? 'Read' : 'Added'} ${plural(accepted.length, 'track')}`,
      ];
      if (leftovers.length > 0) parts.push(leftovers.join(' · '));
      if (readOnly) parts.push('a dropped folder is read-only · choose the card to write back');
      setNotice({ kind: 'success', text: parts.join(' · ') });
    }
  },
    [],
  );

  const addFound = useCallback(
    (found: FoundFile[], skipped: number, readOnly = false) =>
      acceptIntake(found, skipped, false, readOnly),
    [acceptIntake],
  );

  /**
   * Read the card again: the same folder, walked now. It is the gesture for "I added tracks to
   * the card; look again" without reloading the page or picking the folder a second time. The
   * handle comes from this visit's pick, or from the one the browser remembers for the card —
   * in which case the click is also where the grant is asked for again.
   *
   * A load does not do this on its own. The handle outliving a reload is not a reason to walk
   * the card and start measuring: that is a run, and a run is asked for. The empty Queue offers
   * this act as a button, which is where the click that can prompt for the grant lives.
   */
  const rescanCard = useCallback(async () => {
    let handle = cardHandle;
    if (handle === null && storedHandle !== null) {
      /* A remembered handle still needs its grant, and this click is the moment to ask. It is
       * asked for read-write because a rescan is usually the prelude to a run, and a run that
       * cannot write the image back is half a run. */
      if (!(await ensureAccess(storedHandle, 'readwrite'))) {
        setNotice({
          kind: 'error',
          text: 'This browser needs permission to read and write the card again. Open it with “Change card”.',
        });
        return;
      }
      setCardHandle(storedHandle as unknown as SnapshotDirectory);
      handle = storedHandle as unknown as SnapshotDirectory;
    }
    if (handle === null) {
      setNotice({
        kind: 'error',
        text: 'No card is open in this page. Use “Change card” to open one.',
      });
      return;
    }
    try {
      const read = await readOpenCard(handle);
      if ('problem' in read) {
        setNotice({ kind: 'error', text: read.problem });
        return;
      }
      acceptIntake(read.files, read.skipped, true);
    } catch (error) {
      setNotice({
        kind: 'error',
        text: `Could not read the card again: ${error instanceof Error ? error.message : String(error)}`,
      });
    }
  }, [acceptIntake, cardHandle, storedHandle]);

  /** Open another card: the same four steps as the first pick, and the queue becomes its. */
  const changeCard = useCallback(async () => {
    const picked = await readCardWithPicker();
    switch (picked.kind) {
      case 'read':
        setCardHandle(picked.handle);
        acceptIntake(picked.files, picked.skipped, true);
        setNotice(null);
        return;
      case 'wrong':
        setNotice({ kind: 'error', text: picked.problem });
        return;
      case 'busy':
        setNotice({
          kind: 'error',
          text: 'A folder picker is already open. Finish or cancel it, then choose the card again.',
        });
        return;
      case 'failed':
        setNotice({ kind: 'error', text: `Could not read the card: ${picked.message}` });
        return;
      case 'unsupported':
        setNotice({
          kind: 'error',
          text: 'This browser cannot open a folder for writing, so it cannot write k7.db to a card.',
        });
        return;
      case 'cancelled':
        return;
    }
  }, [acceptIntake]);

  /* The card arrived through the picker, so it is writable: remember the handle, and take
   * the files the same way a drop delivers them. */
  const addCard = useCallback(
    (found: FoundFile[], skipped: number, handle: SnapshotDirectory) => {
      setCardHandle(handle);
      addFound(found, skipped);
      setNotice(null);
    },
    [addFound],
  );

  const deepAnalyse = useCallback(
    async (item: QueueItem) => {
      if (!item.analysis || !modelSet || busy) return;
      const client = clients.find((each) => !busyClients.current.has(each));
      if (!client) {
        setNotice({
          kind: 'error',
          text: 'The analysis workers are busy; try again when the run pauses.',
        });
        return;
      }
      busyClients.current.add(client);
      setBusy(true);
      try {
        update(item.id, { status: 'analysing', stage: 'deep' });
        const { file: source, bytes } = await readItem(item);
        const audio = await decodeFile(bytes.buffer);
        const tempo = await client.deep(audio);
        const analysis = { ...item.analysis, tempo };
        await putCached(cacheKey(item.analysis.track.hash, modelSet), analysis);
        askPersistenceOnce();
        update(item.id, { status: 'done', analysis, stage: undefined, cached: false });
        /* The file's XTAPE is now one tempo behind: rewrite the tags so the next pass reads
         * the deep result too. */
        await finalizeTrack(
          { ...item, file: source },
          client,
          analysis,
          item.analysis.track.hash,
        );
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (isWeightError(error)) setWeightError(message);
        setNotice({ kind: 'error', text: `Deep analysis failed: ${message}` });
        update(item.id, { status: 'done', stage: undefined });
      } finally {
        busyClients.current.delete(client);
        setBusy(false);
        setLaneTick((tick) => tick + 1);
      }
    },
    [busy, clients, modelSet, update, finalizeTrack, readItem],
  );

  const saveTags = useCallback(
    async (item: QueueItem) => {
      if (!item.analysis) return;
      update(item.id, { saving: true });
      try {
        /* The bytes come from the card's file; a dead snapshot is reopened once, so a long
         * run's stale queue does not turn saving into a failure. */
        const tagged = await withReadable(item.file, () => reopenItem(item), (file) =>
          writeAnalysisTags(file, item.analysis!),
        );
        /* A dropped folder's only write is this copy, so it carries the label too. Art that
         * will not encode is not a reason to lose the tags: fall back to them. */
        const result = await writeTrackLabel(item.file, undefined, { bytes: tagged }).catch(
          () => null,
        );
        downloadTaggedCopy(
          item.name,
          result?.kind === 'written' ? result.bytes : tagged,
          item.format,
        );
        setNotice({ kind: 'success', text: `Saved a tagged copy of ${item.name}.` });
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (isWeightError(error)) setWeightError(message);
        setNotice({ kind: 'error', text: `Could not write tags: ${message}` });
      } finally {
        update(item.id, { saving: false });
      }
    },
    [update, reopenItem],
  );

  const retry = useCallback(
    (item: QueueItem) => {
      update(item.id, { status: 'queued', error: undefined, stage: undefined });
    },
    [update],
  );

  /**
   * The snapshot is the only thing this page produces that the player consumes: one file,
   * at the card's root, that the device flashes and deletes. It carries the measurements
   * *and* this browser's drafts (ADR 0051); with no Playlist the output is byte-identical
   * version 1, so an image with nothing to say about lists stays the smaller, older format.
   *
   * A Track outside a `music` directory is measured and shown but has no place in it, so
   * the count is reported rather than silently dropped.
   */
  const buildImage = useCallback(async (): Promise<SnapshotExport | null> => {
    if (!manifest) {
      /* A draft can enable a write before the weights are in: the image needs the model set
       * to say which models produced it, so there is nothing honest to build yet. */
      setNotice({
        kind: 'error',
        text: setupError
          ? 'The models did not load, so no image can be built yet.'
          : 'The models are still loading. An image is built with the model set that produced it.',
      });
      return null;
    }
    const media = new Map(
      (await listMedia(currentCardScope())).map((row) => [row.path, row] as const),
    );
    const lists = toSnapshotPlaylists();
    const result = await buildSnapshotExport(
      items.map((item) => ({
        file: item.file,
        analysis: item.analysis ?? null,
        tags: media.get(snapshotPath(item.path) ?? ''),
      })),
      manifest,
      lists,
    );
    if (result.tracks === 0 && lists.every((list) => list.rows.length === 0)) {
      setNotice({
        kind: 'error',
        text:
          'Nothing to write yet: no Track below music/ has been measured, and there is no ' +
          'Playlist to carry. Measure the card, or add a Playlist.',
      });
      return null;
    }
    return result;
  }, [items, manifest, setupError]);

  const leftovers = (skipped: number) =>
    skipped > 0 ? ` ${plural(skipped, 'analysed file')} outside music/ left out.` : '';

  const listMessage = (count: number) => (count > 0 ? ` and ${plural(count, 'playlist')}` : '');

  const nonEmptyLists = () =>
    toSnapshotPlaylists().filter((list) => list.rows.length > 0).length;

  /* What the card last took. The two signatures let an effect tell a new run or a changed
   * list from the state it already wrote, so a settled queue does not rewrite the card on
   * every render. */
  const written = useRef({ run: '', lists: '' });
  /* A write in flight, as a ref: `saving` drives the UI, but the effects below must not
   * re-run merely because a write finished — that would retry a failed one forever. */
  const writing = useRef(false);
  const [writtenAt, setWrittenAt] = useState<string | null>(null);

  const done = items.filter((item) => item.analysis).length;
  const total = items.length;
  /* A run is every queued Track measured: nothing waiting, nothing in flight. The write is
   * debounced because a queue settles one Track at a time and each write is the whole
   * image. */
  const pending = items.filter(
    (item) =>
      item.status === 'queued' ||
      item.status === 'hashing' ||
      item.status === 'decoding' ||
      item.status === 'analysing',
  ).length;
  const settled = total > 0 && pending === 0 && !busy;

  /**
   * The card is the App's only output, so the write happens by itself.
   *
   * `run` builds the image from the queue; `lists` patches this browser's lists into the
   * image already on the card, which is one 2 MB rewrite instead of a re-measurement. Both
   * need the card to have been opened read-write at intake: a dropped folder's permission
   * cannot be raised (ADR 0010), and nothing else can write a card.
   */
  const writeToCard = useCallback(
    async (mode: 'run' | 'lists'): Promise<boolean> => {
      if (!cardHandle || writing.current) return false;
      writing.current = true;
      setSaving(true);
      try {
        let bytes: Uint8Array<ArrayBuffer>;
        let tracks: number;
        let skipped = 0;
        let lists: number;
        if (mode === 'lists' && lastSnapshot) {
          bytes = patchListArea(lastSnapshot.bytes, toSnapshotPlaylists());
          tracks = lastSnapshot.tracks;
          lists = nonEmptyLists();
        } else {
          const built = await buildImage();
          if (!built) return false;
          bytes = built.bytes;
          tracks = built.tracks;
          skipped = built.skipped;
          lists = nonEmptyLists();
        }
        await writeSnapshotToCard(cardHandle, bytes);
        const scope = currentCardScope();
        const stored: StoredSnapshot = {
          scope,
          card: cardRefFor(scope),
          bytes,
          tracks,
          skipped,
          at: new Date().toISOString(),
        };
        await saveSnapshot(scope, stored);
        setLastSnapshot(stored);
        setWrittenAt(stored.at);
        setNotice({
          kind: 'success',
          text:
            (mode === 'lists'
              ? `Wrote ${plural(lists, 'playlist')} to the card’s image.`
              : `Wrote k7.db for ${plural(tracks, 'track')}${listMessage(lists)} to the card.` +
                leftovers(skipped)) + ' Put the card back in the player.',
        });
        return true;
      } catch (error) {
        setNotice({
          kind: 'error',
          text: `Could not write to the card: ${error instanceof Error ? error.message : String(error)}`,
        });
        return false;
      } finally {
        writing.current = false;
        setSaving(false);
      }
    },
    [buildImage, cardHandle, lastSnapshot],
  );


  const listsSignature = drafts
    .map((draft) => `${draft.id}:${draft.name}:${draft.rows.join('|')}`)
    .join(';');

  /* A card is only written once nothing is still moving on it. The queue settling is not
   * the end of a run any more: the label pass keeps rewriting files after the last measure,
   * and each of those writes moves a size the Snapshot reports. Waiting for both is what
   * turns a rewrite per label into one image at the end. */
  const finished = settled && metadataPending === 0;
  useEffect(() => {
    if (!cardHandle || !manifest || !finished || writing.current) return;
    const signature = `${runRevision}:${total}:${done}`;
    if (signature === written.current.run) return;
    const timer = window.setTimeout(() => {
      void writeToCard('run').then((ok) => {
        if (!ok) return;
        written.current.run = signature;
        written.current.lists = listsSignature;
      });
    }, 800);
    return () => window.clearTimeout(timer);
  }, [cardHandle, manifest, finished, runRevision, total, done, listsSignature, writeToCard]);

  /* Playlists ride the image, so editing one after a run has to reach the card too — but
   * only once this session has written an image: a card that has not been measured here is
   * not overwritten with a stored one. */
  useEffect(() => {
    if (!cardHandle || !lastSnapshot || written.current.run === '') return;
    if (listsSignature === written.current.lists) return;
    const timer = window.setTimeout(() => {
      void writeToCard('lists').then((ok) => {
        if (ok) written.current.lists = listsSignature;
      });
    }, 800);
    return () => window.clearTimeout(timer);
  }, [cardHandle, lastSnapshot, listsSignature, writeToCard]);

  /* A dropped card root is read-only, so a finished run has nowhere to go. Say it once,
   * with the one act that fixes it, rather than leaving the user with a measured queue. */
  const toldReadOnly = useRef('');
  useEffect(() => {
    if (cardHandle || !settled) return;
    const signature = `${runRevision}:${total}`;
    if (toldReadOnly.current === signature) return;
    toldReadOnly.current = signature;
    setNotice({
      kind: 'error',
      text:
        `Measured ${plural(done, 'track')}, but k7.db was not written: this card was dropped, ` +
        'so the page can read it and not write it. Open it with “Choose card” once to write ' +
        'the image (nothing has to be measured again).',
    });
  }, [cardHandle, settled, runRevision, total, done]);

  const cardLabel = (() => {
    const reference = cardRefFor(currentCardScope());
    if (reference.mark) return `mark ${reference.mark.slice(0, 8)}…`;
    if (reference.fingerprint) return `unmarked, fingerprint ${reference.fingerprint.slice(0, 8)}…`;
    return 'no card yet';
  })();

  const clearAll = useCallback(async () => {
    setItems([]);
    setNotice(null);
    setCardHandle(null);
    setStoredHandle(null);
    await forgetCardHandle(currentCardScope());
    await clearCache();
  }, []);

  const readCardFile = useCallback(
    async (path: string): Promise<File | null> => {
      let handle = cardHandle;
      /* No handle this visit: the card can still be the one this browser opened before. The
       * grant is requested here, on the click that asked to play, which is the only moment a
       * browser will show it. Read only, and never promoted to the write handle: playing a
       * track must not turn into a promise that `k7.db` can be written back. */
      if (handle === null && storedHandle !== null && (await ensureRead(storedHandle))) {
        handle = storedHandle as unknown as SnapshotDirectory;
      }
      if (handle !== null) {
        try {
          const file = await readFileFromCard(handle as unknown as CardRoot, path);
          if (file !== null) return file;
        } catch {
          // A handle can be revoked under the page; the queue below is the second door.
        }
      }
      const measured = items.find((item) => snapshotPath(item.path) === path);
      return measured?.file ?? null;
    },
    [cardHandle, items, storedHandle],
  );

  const pause = useCallback(() => setRunning(false), []);
  const resume = useCallback(() => setRunning(true), []);

  const value: AnalysisApi = {
    items,
    busy,
    workers: workerCapacity,
    running,
    pause,
    resume,
    busyStage: busy ? runStage(items) : null,
    done,
    total,
    modelSet,
    setupError,
    weightError,
    notice,
    dismissNotice: () => setNotice(null),
    dismissWeightError: () => setWeightError(null),
    mediaCount,
    metadataPending,
    lastSnapshot,
    saving,
    tagsOnly,
    setTagsOnly,
    confirmingClear,
    setConfirmingClear,
    addFound,
    addCard,
    rescanCard,
    changeCard,
    hasCardHandle: cardHandle !== null,
    canRescan: cardHandle !== null || storedHandle !== null,
    readCardFile,
    retry,
    deepAnalyse,
    saveTags,
    writtenAt,
    cardLabel,
    clearAll,
  };

  return <AnalysisContext.Provider value={value}>{children}</AnalysisContext.Provider>;
}

/** The analysis state. Throws outside the provider, which is a mounting mistake, not a state. */
export function useAnalysis(): AnalysisApi {
  const value = useContext(AnalysisContext);
  if (!value) throw new Error('useAnalysis must be used inside <AnalysisProvider>');
  return value;
}
