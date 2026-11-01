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
import { readTrackMetadata } from '../../audio/metadata';
import { downloadTaggedCopy, writeAnalysisTags } from '../../audio/tagWriter';
import {
  ANALYSIS_SCHEMA,
  type Analysis,
  type ExportDocument,
  type ModelManifest,
} from '../../domain/analysis';
import { snapshotPath } from '../../domain/snapshot';
import { needsMeasurement, tagAnalysis } from '../../domain/tagAnalysis';
import { sendSnapshot } from '../../device/upload';
import { newId } from '../../lib/id';
import { toSnapshotPlaylists, useDrafts } from '../../playlists/drafts';
import { allMedia, countMedia, putMedia } from '../../storage/media';
import {
  clearSnapshot,
  loadSnapshot,
  saveSnapshot,
  type StoredSnapshot,
} from '../../storage/snapshots';
import { clearCache, getCached, putCached } from '../../storage/cache';
import { cacheKey } from '../../config';
import { isWeightError } from '../../weights/errors';
import { writeSnapshotToCard, type SnapshotDirectory } from '../cardWriter';
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
  /** The card was opened read-write in this session, so it can be written back to. */
  hasCardHandle: boolean;

  retry: (item: QueueItem) => void;
  deepAnalyse: (item: QueueItem) => Promise<void>;
  saveTags: (item: QueueItem) => Promise<void>;
  exportJson: () => void;

  /** Builds `xtape.db` from the queue and this browser's drafts. */
  buildImage: () => Promise<SnapshotExport | null>;
  sendToPlayer: () => Promise<void>;
  saveToCard: () => Promise<void>;
  saveSnapshotFile: () => Promise<void>;
  resendLast: () => Promise<void>;
  downloadLast: () => void;
  discardLast: () => Promise<void>;
  clearAll: () => Promise<void>;

  /** Something the player could be given: a measured track below `music/`, or a list. */
  canSave: boolean;
}

const AnalysisContext = createContext<AnalysisApi | null>(null);

export function AnalysisProvider({ children }: { children: ReactNode }) {
  const [items, setItems] = useState<QueueItem[]>([]);
  const [busy, setBusy] = useState(false);
  const [manifest, setManifest] = useState<ModelManifest | null>(null);
  const modelSet = manifest?.modelSet ?? null;
  const [saving, setSaving] = useState(false);
  const [tagsOnly, setTagsOnly] = useState(true);
  const [lastSnapshot, setLastSnapshot] = useState<StoredSnapshot | null>(null);
  const [mediaCount, setMediaCount] = useState(0);
  const [confirmingClear, setConfirmingClear] = useState(false);
  /* The card the user opened read-write, when they opened one: the image goes back there
   * without asking again. A dropped folder leaves this null. */
  const [cardHandle, setCardHandle] = useState<SnapshotDirectory | null>(null);
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
    void countMedia().then(setMediaCount).catch(() => {});
    loadSnapshot()
      .then((stored) => {
        if (!cancelled && stored) setLastSnapshot(stored);
      })
      .catch(() => {
        /* A store that cannot be read is not a reason to fail the page. */
      });
    return () => {
      cancelled = true;
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
  const rememberMedia = useCallback(async (item: QueueItem) => {
    const path = snapshotPath(item.path);
    if (!path) return;
    try {
      const { parseBlob } = await import('music-metadata');
      const parsed = await parseBlob(item.file, { duration: false, skipCovers: false });
      const picture = parsed.common.picture?.[0];
      await putMedia({
        path,
        title: parsed.common.title ?? '',
        artist: parsed.common.artist ?? '',
        album: parsed.common.album ?? '',
        genre: parsed.common.genre?.[0] ?? '',
        cover: picture
          ? { bytes: new Uint8Array(picture.data), format: picture.format }
          : undefined,
        at: new Date().toISOString(),
      });
      void countMedia().then((n) => setMediaCount(n));
    } catch {
      /* Tags and art are a convenience: a file without them is measured like any other. */
    }
  }, []);

  const update = useCallback((id: string, patch: Partial<QueueItem>) => {
    setItems((previous) =>
      previous.map((item) => (item.id === id ? { ...item, ...patch } : item)),
    );
  }, []);

  const processItem = useCallback(
    async (item: QueueItem, client: AnalysisClient) => {
      if (!modelSet) return;
      try {
        update(item.id, { status: 'hashing', error: undefined });
        const buffer = await item.file.arrayBuffer();
        const hash = await sha256Hex(buffer);
        const key = cacheKey(hash, modelSet);

        update(item.id, { status: 'decoding' });
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
          return;
        }

        const metadata = await readTrackMetadata(item.file);

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
          update(item.id, { status: 'tagged', analysis, stage: undefined });
          void rememberMedia(item);
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
        update(item.id, { status: 'done', analysis, stage: undefined });
        void rememberMedia(item);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (isWeightError(error)) setWeightError(message);
        update(item.id, { status: 'error', error: message });
      } finally {
        claimed.current.delete(item.id);
      }
    },
    [modelSet, update, tagsOnly, rememberMedia],
  );

  /* Keep every lane busy: claim as many queued items as there are free workers, hand each to
   * a different client, and let them finish in any order. The claim set exists because the
   * effect re-runs on every status change and would otherwise hand the same item out twice. */
  useEffect(() => {
    if (!modelSet) return;
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
  }, [items, modelSet, processItem, clients]);

  const addFound = useCallback((found: FoundFile[], skipped: number, readOnly = false) => {
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
      setItems((previous) => [...previous, ...accepted]);
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
    if (rejected.length > 0) leftovers.push(`${plural(rejected.length, 'non-audio file')} ignored`);
    if (skipped > 0) leftovers.push(`${plural(skipped, 'item')} unreadable`);

    if (leftovers.length > 0 || readOnly) {
      const parts = [`Added ${plural(accepted.length, 'track')}`];
      if (leftovers.length > 0) parts.push(leftovers.join(' · '));
      if (readOnly) parts.push('a dropped folder is read-only · choose the card to write back');
      setNotice({ kind: 'success', text: parts.join(' · ') });
    }
  }, []);

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

  const exportJson = useCallback(() => {
    const analyses = items.flatMap((item) => (item.analysis ? [item.analysis] : []));
    if (analyses.length === 0) return;
    const payload: ExportDocument = {
      schema: ANALYSIS_SCHEMA,
      generatedAt: new Date().toISOString(),
      analyses,
    };
    const blob = new Blob([JSON.stringify(payload, null, 2)], {
      type: 'application/json',
    });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `xtape-analysis-${new Date().toISOString().slice(0, 10)}.json`;
    anchor.click();
    URL.revokeObjectURL(url);
  }, [items]);

  /**
   * The snapshot is the only thing this page produces that the player consumes: one
   * file, at the card's root, that the device flashes and deletes. It carries the
   * measurements *and* this browser's drafts (ADR 0051); with no Playlist the output is
   * byte-identical version 1, so an image with nothing to say about lists stays the
   * smaller, older format.
   *
   * A Track outside a `music` directory is measured and shown but has no place in it, so
   * the count is reported rather than silently dropped.
   */
  const buildImage = useCallback(async (): Promise<SnapshotExport | null> => {
    if (!manifest) {
      /* A draft can enable Send before the weights are in: the image needs the model set
       * to say which models produced it, so there is nothing honest to build yet. */
      setNotice({
        kind: 'error',
        text: setupError
          ? 'The models did not load, so no image can be built yet.'
          : 'The models are still loading. An image is built with the model set that produced it.',
      });
      return null;
    }
    const media = await allMedia();
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
          'Nothing to write: drop the card root (or its music folder) so the tracks have a ' +
          'path below music/, or add a Playlist.',
      });
      return null;
    }
    return result;
  }, [items, manifest, setupError]);

  const leftovers = (skipped: number) =>
    skipped > 0 ? ` ${plural(skipped, 'analysed file')} outside music/ left out.` : '';

  /* An image may carry lists and no measurements; saying so is the difference between a
   * send that is worth doing and one that looks like a lie. */
  const listMessage = (count: number) =>
    count > 0 ? ` and ${plural(count, 'playlist')}` : '';

  const nonEmptyLists = () =>
    toSnapshotPlaylists().filter((list) => list.rows.length > 0).length;

  /* Writing is only offered on a card the page opened read-write at intake. A dropped
   * folder never is one - a dropped handle's permission cannot be raised (ADR 0010) -
   * so there is no fallback picker here to surprise anyone with: the button that would
   * call this is not rendered without the handle. */
  const saveToCard = useCallback(async () => {
    if (!cardHandle || saving) return;
    setSaving(true);
    try {
      const result = await buildImage();
      if (!result) return;
      await writeSnapshotToCard(cardHandle, result.bytes);
      setNotice({
        kind: 'success',
        text:
          `Wrote xtape.db for ${plural(result.tracks, 'track')}` +
          `${listMessage(nonEmptyLists())} to the card.` +
          leftovers(result.skipped) +
          ' Put the card back in the player.',
      });
    } catch (error) {
      setNotice({
        kind: 'error',
        text: `Could not write to the card: ${error instanceof Error ? error.message : String(error)}`,
      });
    } finally {
      setSaving(false);
    }
  }, [buildImage, cardHandle, saving]);

  /* The card stays in the computer; the measurements go to the player over the network.
   * The player validates the image before it writes a byte of flash and restarts itself
   * afterwards, so the honest thing to say here is "it is restarting". */
  const sendToPlayer = useCallback(async () => {
    if (saving) return;
    setSaving(true);
    try {
      const result = await buildImage();
      if (!result) return;
      const stored: StoredSnapshot = {
        bytes: result.bytes,
        tracks: result.tracks,
        skipped: result.skipped,
        at: new Date().toISOString(),
      };
      await saveSnapshot(stored);
      const sent = await sendSnapshot(result.bytes);
      const accepted: StoredSnapshot = { ...stored, sentAt: new Date().toISOString() };
      await saveSnapshot(accepted);
      setLastSnapshot(accepted);
      setNotice({
        kind: 'success',
        text:
          `Sent ${plural(result.tracks, 'track')}${listMessage(nonEmptyLists())} to the ` +
          `player (${sent.bytes} bytes).` +
          leftovers(result.skipped) +
          ' It is restarting to pick up the new measurements.',
      });
    } catch (error) {
      setNotice({
        kind: 'error',
        text:
          `Could not send to the player: ${error instanceof Error ? error.message : String(error)}. ` +
          'The image is kept — send it again when the player is back.',
      });
    } finally {
      setSaving(false);
    }
  }, [buildImage, saving]);

  const saveSnapshotFile = useCallback(async () => {
    if (saving) return;
    setSaving(true);
    try {
      const result = await buildImage();
      if (!result) return;
      const stored: StoredSnapshot = {
        bytes: result.bytes,
        tracks: result.tracks,
        skipped: result.skipped,
        at: new Date().toISOString(),
      };
      await saveSnapshot(stored);
      setLastSnapshot(stored);
      const blob = new Blob([result.bytes], { type: 'application/octet-stream' });
      const url = URL.createObjectURL(blob);
      const anchor = document.createElement('a');
      anchor.href = url;
      anchor.download = 'xtape.db';
      anchor.click();
      URL.revokeObjectURL(url);
      setNotice({
        kind: 'success',
        text:
          `Saved xtape.db for ${plural(result.tracks, 'track')}` +
          `${listMessage(nonEmptyLists())}.` +
          leftovers(result.skipped) +
          " Put it at the card's root and put the card back.",
      });
    } catch (error) {
      setNotice({
        kind: 'error',
        text: `Could not write the snapshot: ${error instanceof Error ? error.message : String(error)}`,
      });
    } finally {
      setSaving(false);
    }
  }, [buildImage, saving]);

  /* Sending again is the whole point of keeping the image: the player restarts after it
   * accepts one, and a page that had to re-measure the card to try again would punish the
   * one moment it cannot avoid. */
  const resendLast = useCallback(async () => {
    if (!lastSnapshot || saving) return;
    setSaving(true);
    try {
      const sent = await sendSnapshot(lastSnapshot.bytes);
      const accepted = { ...lastSnapshot, sentAt: new Date().toISOString() };
      await saveSnapshot(accepted);
      setLastSnapshot(accepted);
      setNotice({
        kind: 'success',
        text:
          `Sent ${plural(lastSnapshot.tracks, 'track')} to the player (${sent.bytes} bytes). ` +
          'It is restarting to pick up the new measurements.',
      });
    } catch (error) {
      setNotice({
        kind: 'error',
        text: `Could not send to the player: ${error instanceof Error ? error.message : String(error)}`,
      });
    } finally {
      setSaving(false);
    }
  }, [lastSnapshot, saving]);

  const downloadLast = useCallback(() => {
    if (!lastSnapshot) return;
    const blob = new Blob([lastSnapshot.bytes], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = 'xtape.db';
    anchor.click();
    URL.revokeObjectURL(url);
  }, [lastSnapshot]);

  const discardLast = useCallback(async () => {
    await clearSnapshot();
    setLastSnapshot(null);
  }, []);

  const clearAll = useCallback(async () => {
    setItems([]);
    setNotice(null);
    await clearCache();
  }, []);

  const done = items.filter((item) => item.analysis).length;
  const total = items.length;
  const canSave =
    items.some((item) => item.analysis && snapshotPath(item.analysis.track.path) !== null) ||
    drafts.some((draft) => draft.rows.length > 0);

  const value: AnalysisApi = {
    items,
    busy,
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
    hasCardHandle: cardHandle !== null,
    retry,
    deepAnalyse,
    saveTags,
    exportJson,
    buildImage,
    sendToPlayer,
    saveToCard,
    saveSnapshotFile,
    resendLast,
    downloadLast,
    discardLast,
    clearAll,
    canSave,
  };

  return <AnalysisContext.Provider value={value}>{children}</AnalysisContext.Provider>;
}

/** The analysis state. Throws outside the provider, which is a mounting mistake, not a state. */
export function useAnalysis(): AnalysisApi {
  const value = useContext(AnalysisContext);
  if (!value) throw new Error('useAnalysis must be used inside <AnalysisProvider>');
  return value;
}
