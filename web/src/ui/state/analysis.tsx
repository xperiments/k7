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
import { decodeFile, sha256Hex, trackFormat } from '../../audio/decode';
import { loadManifest } from '../../audio/models';
import { readTrackMetadata, type TrackTags } from '../../audio/metadata';
import { downloadTaggedCopy, writeAnalysisTags } from '../../audio/tagWriter';
import type { Analysis, ModelManifest } from '../../domain/analysis';
import { snapshotPath } from '../../domain/snapshot';
import { needsMeasurement, tagAnalysis } from '../../domain/tagAnalysis';
import { newId } from '../../lib/id';
import { toSnapshotPlaylists, useDrafts } from '../../playlists/drafts';
import { patchListArea } from '../../playlists/image';
import { countMedia, getMedia, listMedia, measuredHash, putMedia, tagsOf } from '../../storage/media';
import { cardRefFor, currentCardScope, subscribeCardScope } from '../../card/session';
import { readCardFile as readFileFromCard, type CardRoot } from '../../card/read';
import {
  cardHandleFor,
  ensureAccess,
  ensureRead,
  forgetCardHandle,
  queryAccess,
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
  /**
   * Whether queued rows are being handed to the workers. A card read on load arrives **held**
   * (`false`): the queue is its contents, but a load must not become hours of measuring the
   * card again. Nothing else pauses it; a manual read starts it.
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
  /* Held only by a load-time read; every other intake starts a run. */
  const [running, setRunning] = useState(true);
  /** The Scope the page already tried to pick up on load, so a failure is not a loop. */
  const reconnected = useRef<string | null>(null);
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

  /* Three workers, not one. Measuring is CPU-bound and a worker is a thread, so the queue
   * used to leave every other core idle; the pool is what turns a whole card from a sequence
   * into a few parallel lanes. Each worker loads its own model sessions, which is the price
   * of the parallelism and the reason the pool is small. */
  const [clients] = useState(() => Array.from({ length: 3 }, () => new AnalysisClient()));
  const nextClient = useRef(0);
  const inFlight = useRef(0);
  const claimed = useRef(new Set<string>());

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

  /* The card is in the computer exactly once, and the browser is the only place a cover can
   * be kept: the player serves paths and measurements, never a picture. So every track that
   * settles leaves its tags and its art here, keyed by the Snapshot's own path, and the
   * library views join the two by that key. */
  const rememberMedia = useCallback(async (item: QueueItem, hash: string) => {
    const path = snapshotPath(item.path);
    if (!path) return;
    try {
      const { parseBlob } = await import('music-metadata');
      const parsed = await parseBlob(item.file, { duration: false, skipCovers: false });
      const picture = parsed.common.picture?.[0];
      await putMedia(currentCardScope(), {
        path,
        title: parsed.common.title ?? '',
        artist: parsed.common.artist ?? '',
        album: parsed.common.album ?? '',
        genre: parsed.common.genre?.[0] ?? '',
        cover: picture
          ? { bytes: new Uint8Array(picture.data), format: picture.format }
          : undefined,
        at: new Date().toISOString(),
        size: item.file.size,
        mtime: item.file.lastModified,
        hash,
      });
      void countMedia(currentCardScope()).then((n) => setMediaCount(n));
    } catch {
      /* Tags and art are a convenience: a file without them is measured like any other. */
    }
  }, []);

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
    async (item: QueueItem): Promise<{ analysis: Analysis; tags: TrackTags } | null> => {
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
      };
    },
    [modelSet],
  );

  const update = useCallback((id: string, patch: Partial<QueueItem>) => {
    setItems((previous) =>
      previous.map((item) => (item.id === id ? { ...item, ...patch } : item)),
    );
    if (patch.analysis !== undefined) setRunRevision((revision) => revision + 1);
  }, []);

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
          return;
        }
        update(item.id, { status: 'hashing', error: undefined });
        const buffer = await item.file.arrayBuffer();
        const hash = await sha256Hex(buffer);
        const key = cacheKey(hash, modelSet);

        update(item.id, { status: 'decoding' });

        /* The file's own tags are read before the cache is consulted. They are not part of a
         * stored analysis — the Process row shows the genre, and a cached Track would
         * otherwise be the one row without it — and the Snapshot export reuses them instead
         * of re-reading the file. */
        const metadata = await readTrackMetadata(item.file);
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
              size: item.file.size,
              hash,
            },
          };
          update(item.id, { status: 'done', analysis, cached: true });
          // The analysis is keyed by the content's hash, not by the card: this file may be
          // known from another card while this card's shelf has never seen it. The row is
          // what Media joins on, so measuring it once here is what keeps "processed" from
          // being reported as "not measured".
          void rememberMedia(item, hash);
          return;
        }

        /* A track whose tags already carry BPM and key needs no inference: the row is built
         * from the tags and the fields they cannot carry stay unknown. */
        if (!needsMeasurement(metadata, tagsOnly)) {
          const analysis = tagAnalysis(
            {
              name: item.file.name,
              path: item.path,
              size: item.file.size,
              format: item.format,
              hash,
            },
            metadata,
          );
          await putCached(key, analysis);
        askPersistenceOnce();
          update(item.id, { status: 'tagged', analysis, stage: undefined });
          void rememberMedia(item, hash);
          return;
        }

        const need44k = metadata.bpm === undefined || metadata.key === undefined;
        const audio = await decodeFile(buffer, need44k);
        update(item.id, { status: 'analysing', stage: undefined });
        const analysis = await client.analyze(
          {
            name: item.file.name,
            path: item.path,
            size: item.file.size,
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
        void rememberMedia(item, hash);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (isWeightError(error)) setWeightError(message);
        update(item.id, { status: 'error', error: message });
      } finally {
        claimed.current.delete(item.id);
      }
    },
    [modelSet, update, tagsOnly, rememberMedia, knownAnalysis],
  );

  /* Keep every lane busy: claim as many queued items as there are free workers, hand each to
   * a different client, and let them finish in any order. The claim set exists because the
   * effect re-runs on every status change and would otherwise hand the same item out twice. */
  useEffect(() => {
    if (!modelSet || !running) return;
    for (;;) {
      if (inFlight.current >= clients.length) break;
      const next = items.find(
        (item) => item.status === 'queued' && !claimed.current.has(item.id),
      );
      if (!next) break;
      claimed.current.add(next.id);
      const client = clients[nextClient.current++ % clients.length];
      inFlight.current += 1;
      setBusy(true);
      void processItem(next, client).finally(() => {
        inFlight.current -= 1;
        if (inFlight.current === 0) setBusy(false);
      });
    }
  }, [items, modelSet, processItem, clients, running]);

  /**
   * Take a card's files into the queue.
   *
   * `replace` is what a re-read of the same card means: the queue *is* that card, so its
   * rows are the card's rows. Measuring again is cheap — everything the browser already has
   * is recognised from the card's own record — and appending would double every row.
   */
  const acceptIntake = useCallback(
    (
      found: FoundFile[],
      skipped: number,
      replace: boolean,
      readOnly = false,
      start: 'run' | 'held' = 'run',
    ) => {
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
      // A card read on load is held; anything a person did starts measuring at once.
      setRunning(start === 'run');
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

  /**
   * The card a reload left behind: the handle is remembered, the grant usually is not, and the
   * browser will not prompt without a person. So a load **asks itself** — `queryAccess` never
   * prompts — and only when the answer is already yes does it walk the card. What it reads
   * arrives **held**: the queue is the card's contents, but measuring starts on the button
   * ("Measure N tracks"), because a reload must never become the card's whole run.
   */
  useEffect(() => {
    if (storedHandle === null || items.length > 0 || busy) return;
    const scope = currentCardScope();
    if (reconnected.current === scope) return;
    reconnected.current = scope;

    let cancelled = false;
    void (async () => {
      try {
        if (!(await queryAccess(storedHandle, 'read'))) return;
        if (cancelled) return;
        if (await queryAccess(storedHandle, 'readwrite')) {
          setCardHandle(storedHandle as unknown as SnapshotDirectory);
        }
        const read = await readOpenCard(storedHandle as unknown as SnapshotDirectory);
        if (cancelled || 'problem' in read) return;
        setRunning(false);
        acceptIntake(read.files, read.skipped, true, false, 'held');
        setNotice({
          kind: 'success',
          text: `The card this browser remembers is in: ${plural(read.files.length, 'file')} read, not measured. Press Measure when you want the run.`,
        });
      } catch {
        /* The card is out, or the handle no longer points at it: nothing to say on load. */
      }
    })();

    return () => {
      cancelled = true;
    };
  }, [storedHandle, items.length, busy, acceptIntake]);

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
      setBusy(true);
      try {
        update(item.id, { status: 'analysing', stage: 'deep' });
        const buffer = await item.file.arrayBuffer();
        const audio = await decodeFile(buffer);
        const tempo = await clients[0].deep(audio);
        const analysis = { ...item.analysis, tempo };
        await putCached(cacheKey(item.analysis.track.hash, modelSet), analysis);
        askPersistenceOnce();
        update(item.id, { status: 'done', analysis, stage: undefined, cached: false });
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (isWeightError(error)) setWeightError(message);
        setNotice({ kind: 'error', text: `Deep analysis failed: ${message}` });
        update(item.id, { status: 'done', stage: undefined });
      } finally {
        setBusy(false);
      }
    },
    [busy, clients, modelSet, update],
  );

  const saveTags = useCallback(
    async (item: QueueItem) => {
      if (!item.analysis) return;
      update(item.id, { saving: true });
      try {
        const bytes = await writeAnalysisTags(item.file, item.analysis);
        downloadTaggedCopy(item.name, bytes, item.format);
        setNotice({ kind: 'success', text: `Saved a tagged copy of ${item.name}.` });
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (isWeightError(error)) setWeightError(message);
        setNotice({ kind: 'error', text: `Could not write tags: ${message}` });
      } finally {
        update(item.id, { saving: false });
      }
    },
    [update],
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

  useEffect(() => {
    if (!cardHandle || !manifest || !settled || writing.current) return;
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
  }, [cardHandle, manifest, settled, runRevision, total, done, listsSignature, writeToCard]);

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
