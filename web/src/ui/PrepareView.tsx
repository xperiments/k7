import { useCallback, useEffect, useRef, useState } from 'react';
import { AnalysisClient } from '../audio/client';
import { decodeFile, sha256Hex, trackFormat } from '../audio/decode';
import { loadManifest } from '../audio/models';
import { readTrackMetadata } from '../audio/metadata';
import { downloadTaggedCopy, writeAnalysisTags } from '../audio/tagWriter';
import {
  ANALYSIS_SCHEMA,
  type Analysis,
  type ExportDocument,
  type ModelManifest,
} from '../domain/analysis';
import { snapshotPath } from '../domain/snapshot';
import { sendSnapshot } from '../device/upload';
import { newId } from '../lib/id';
import {
  clearSnapshot,
  loadSnapshot,
  saveSnapshot,
  type StoredSnapshot,
} from '../storage/snapshots';
import { needsMeasurement, tagAnalysis } from '../domain/tagAnalysis';
import { SOURCE_URL, cacheKey } from '../config';
import { clearCache, getCached, putCached } from '../storage/cache';
import { WEIGHTS_MODE } from '../weights/config';
import { isWeightError } from '../weights/errors';
import { DropZone } from './DropZone';
import type { FoundFile } from './files';
import { formatBytes, plural, shortHash } from './format';
import { extractCovers } from './covers';
import { QueueList } from './QueueList';
import { buildSnapshotExport } from './snapshotExport';
import { writeSnapshotToCard, type SnapshotDirectory } from './cardWriter';
import type { QueueItem } from './queue';

export interface PrepareViewProps {
  /** Lets the shell hold the view while a track is still being measured (S3). */
  onBusyChange?: (busy: boolean) => void;
}

export function PrepareView({ onBusyChange }: PrepareViewProps) {
  const [items, setItems] = useState<QueueItem[]>([]);
  const [busy, setBusy] = useState(false);
  const [manifest, setManifest] = useState<ModelManifest | null>(null);
  const modelSet = manifest?.modelSet ?? null;
  const [savingSnapshot, setSavingSnapshot] = useState(false);
  const [tagsOnly, setTagsOnly] = useState(true);
  const [compact, setCompact] = useState(true);
  const [lastSnapshot, setLastSnapshot] = useState<StoredSnapshot | null>(null);
  const [covers, setCovers] = useState<string | null>(null);
  /* The card the user opened read-write, when they opened one: the snapshot goes back
   * there without asking again. A dropped folder leaves this null. */
  const [cardHandle, setCardHandle] = useState<SnapshotDirectory | null>(null);
  const [setupError, setSetupError] = useState<string | null>(null);
  const [weightError, setWeightError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  /* Three workers, not one. Measuring is CPU-bound and a worker is a thread, so the queue
   * used to leave every other core idle; the pool is what turns a whole card from a sequence
   * into a few parallel lanes. Each worker loads its own model sessions, which is the price
   * of the parallelism and the reason the pool is small. */
  const [clients] = useState(() =>
    Array.from({ length: 3 }, () => new AnalysisClient()),
  );
  const nextClient = useRef(0);
  const inFlight = useRef(0);
  const claimed = useRef(new Set<string>());

  useEffect(() => {
    onBusyChange?.(busy);
  }, [busy, onBusyChange]);

  /* The last image survives a reload, which is what makes the player's restart cheap: the
   * page can lose its origin for a second and still have the run to send again. */
  useEffect(() => {
    let cancelled = false;
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
      .then((manifest) => {
        if (!cancelled) setManifest(manifest);
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
            { name: item.file.name, path: item.path, size: item.file.size, format: item.format, hash },
            metadata,
          );
          await putCached(key, analysis);
          update(item.id, { status: 'tagged', analysis, stage: undefined });
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
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (isWeightError(error)) setWeightError(message);
        update(item.id, { status: 'error', error: message });
      } finally {
        claimed.current.delete(item.id);
      }
    },
    [modelSet, update, tagsOnly],
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

  /* The card arrived through the picker, so it is writable: remember the handle, and take
   * the files the same way a drop delivers them. */
  const addCard = (found: FoundFile[], skipped: number, handle: SnapshotDirectory) => {
    setCardHandle(handle);
    addFound(found, skipped);
    setNotice(null);
  };

  const addFound = (found: FoundFile[], skipped: number, readOnly = false) => {
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
      setNotice(
        found.length === 0
          ? 'No MP3 or FLAC files found.'
          : `No MP3 or FLAC files in ${plural(found.length, 'file')}.`,
      );
      return;
    }

    const leftovers: string[] = [];
    if (rejected.length > 0) leftovers.push(`${plural(rejected.length, 'non-audio file')} ignored`);
    if (skipped > 0) leftovers.push(`${plural(skipped, 'item')} unreadable`);

    if (leftovers.length > 0 || readOnly) {
      const parts = [`Added ${plural(accepted.length, 'track')}`];
      if (leftovers.length > 0) parts.push(leftovers.join(' · '));
      if (readOnly) parts.push('a dropped folder is read-only · choose the card to write back');
      setNotice(parts.join(' · '));
    }
  };

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
        setNotice(`Deep analysis failed: ${message}`);
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
        setNotice(`Saved a tagged copy of ${item.name}.`);
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        if (isWeightError(error)) setWeightError(message);
        setNotice(`Could not write tags: ${message}`);
      } finally {
        update(item.id, { saving: false });
      }
    },
    [update],
  );

  const retry = (item: QueueItem) => {
    update(item.id, { status: 'queued', error: undefined, stage: undefined });
  };

  const exportJson = () => {
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
  };

  /* The snapshot is the only thing this page produces that the player consumes: one
   * file, at the card's root, that the device flashes and deletes. A Track outside a
   * `music` directory is measured and shown but has no place in it, so the count is
   * reported rather than silently dropped. */
  const buildImage = useCallback(async () => {
    if (!manifest) return null;
    const result = await buildSnapshotExport(
      items.map((item) => ({ file: item.file, analysis: item.analysis ?? null })),
      manifest,
    );
    if (result.tracks === 0) {
      setNotice(
        'Nothing to write: drop the card root (or its music folder) so the tracks have a ' +
          'path below music/.',
      );
      return null;
    }
    return result;
  }, [items, manifest]);

  const leftovers = (skipped: number) =>
    skipped > 0 ? ` ${plural(skipped, 'analysed file')} outside music/ left out.` : '';

  /* Writing is only offered on a card the page opened read-write at intake. A dropped
   * folder never is one - a dropped handle's permission cannot be raised (ADR 0010) -
   * so there is no fallback picker here to surprise anyone with: the button that would
   * call this is not rendered without the handle. */
  const saveToCard = useCallback(async () => {
    if (!cardHandle || savingSnapshot) return;
    setSavingSnapshot(true);
    try {
      const result = await buildImage();
      if (!result) return;
      await writeSnapshotToCard(cardHandle, result.bytes);
      setNotice(
        `Wrote xtape.db for ${plural(result.tracks, 'track')} to the card.` +
          leftovers(result.skipped) +
          ' Put the card back in the player.',
      );
    } catch (error) {
      setNotice(
        `Could not write to the card: ${error instanceof Error ? error.message : String(error)}`,
      );
    } finally {
      setSavingSnapshot(false);
    }
  }, [buildImage, cardHandle, savingSnapshot]);

  /* The card stays in the computer; the measurements go to the player over the network.
   * The player validates the image before it writes a byte of flash and restarts itself
   * afterwards, so the honest thing to say here is "it is restarting". */
  const sendToPlayer = useCallback(async () => {
    if (savingSnapshot) return;
    setSavingSnapshot(true);
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
      setNotice(
        `Sent ${plural(result.tracks, 'track')} to the player (${sent.bytes} bytes).` +
          leftovers(result.skipped) +
          ' It is restarting to pick up the new measurements.',
      );
    } catch (error) {
      setNotice(
        `Could not send to the player: ${error instanceof Error ? error.message : String(error)}. ` +
          'The image is kept — send it again when the player is back.',
      );
    } finally {
      setSavingSnapshot(false);
    }
  }, [buildImage, savingSnapshot]);

  const saveSnapshotFile = useCallback(async () => {
    if (savingSnapshot) return;
    setSavingSnapshot(true);
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
      setNotice(
        `Saved xtape.db for ${plural(result.tracks, 'track')}.` +
          leftovers(result.skipped) +
          " Put it at the card's root and put the card back.",
      );
    } catch (error) {
      setNotice(
        `Could not write the snapshot: ${error instanceof Error ? error.message : String(error)}`,
      );
    } finally {
      setSavingSnapshot(false);
    }
  }, [buildImage, savingSnapshot]);

  /* Sending again is the whole point of keeping the image: the player restarts after it
   * accepts one, and a page that had to re-measure the card to try again would punish the
   * one moment it cannot avoid. */
  const resendLast = useCallback(async () => {
    if (!lastSnapshot || savingSnapshot) return;
    setSavingSnapshot(true);
    try {
      const sent = await sendSnapshot(lastSnapshot.bytes);
      const accepted = { ...lastSnapshot, sentAt: new Date().toISOString() };
      await saveSnapshot(accepted);
      setLastSnapshot(accepted);
      setNotice(
        `Sent ${plural(lastSnapshot.tracks, 'track')} to the player (${sent.bytes} bytes). ` +
          'It is restarting to pick up the new measurements.',
      );
    } catch (error) {
      setNotice(
        `Could not send to the player: ${error instanceof Error ? error.message : String(error)}`,
      );
    } finally {
      setSavingSnapshot(false);
    }
  }, [lastSnapshot, savingSnapshot]);

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

  /* Every cover the files carry, in one archive laid out like the card. It reads each file
   * again — the analysis path skips covers on purpose, because most runs do not need them. */
  const downloadCovers = useCallback(async () => {
    if (items.length === 0) return;
    setCovers('Reading covers…');
    try {
      const result = await extractCovers(
        items.map((item) => ({ file: item.file, path: item.path })),
        (progress) => {
          if (progress.name) setCovers(`Reading covers… ${progress.done}/${progress.total}`);
        },
      );
      if (result.covers === 0) {
        setCovers('No file carried album art.');
        return;
      }
      const blob = new Blob([result.archive], { type: 'application/zip' });
      const url = URL.createObjectURL(blob);
      const anchor = document.createElement('a');
      anchor.href = url;
      anchor.download = 'xtape-covers.zip';
      anchor.click();
      URL.revokeObjectURL(url);
      setCovers(
        `${plural(result.covers, 'cover')} in xtape-covers.zip` +
          (result.missing.length
            ? `, ${result.missing.length} file${result.missing.length === 1 ? '' : 's'} without art`
            : ''),
      );
    } catch (error) {
      setCovers(
        `Could not read the covers: ${error instanceof Error ? error.message : String(error)}`,
      );
    }
  }, [items]);

  const clearAll = async () => {
    setItems([]);
    setNotice(null);
    await clearCache();
  };

  const done = items.filter((item) => item.analysis).length;
  const canSave = items.some(
    (item) => item.analysis && snapshotPath(item.analysis.track.path) !== null,
  );

  return (
    <div className="app">
      <header className="top">
        <div>
          <h1>
            xtape <span>track analysis</span>
          </h1>
          <p className="muted small">
            Tempo, key, mood and energy from MP3 &amp; FLAC, measured in your browser.
          </p>
        </div>
        <div className="model-status small">
          {modelSet ? (
            <span className="ok">models {shortHash(modelSet)}</span>
          ) : setupError ? (
            <span className="bad">models missing</span>
          ) : (
            <span className="muted">loading models…</span>
          )}
        </div>
      </header>

      {setupError && (
        <div className="banner error-banner">
          <div>
            <strong>{setupError}</strong>
            <p className="small">
              {WEIGHTS_MODE === 'local' ? (
                <>
                  Fetch the weights once: <code>cd web &amp;&amp; npm run models</code>
                </>
              ) : (
                'Connect this computer to the internet once, then reload.'
              )}
            </p>
          </div>
        </div>
      )}

      {weightError && (
        <div className="banner error-banner">
          <div>
            <strong>{weightError}</strong>
          </div>
          <button type="button" className="ghost" onClick={() => setWeightError(null)}>
            Dismiss
          </button>
        </div>
      )}

      {notice && (
        <div className="banner">
          <span className="small">{notice}</span>
          <button type="button" className="ghost" onClick={() => setNotice(null)}>
            Dismiss
          </button>
        </div>
      )}

      <DropZone
        disabled={!modelSet}
        onFiles={(found, skipped) => addFound(found, skipped, true)}
        onCard={addCard}
      />

      <div className="toolbar">
        <span className="muted small">
          {done} of {items.length} analysed
        </span>
        <label className="muted small toggle" title="Use BPM and key from the file's own tags and measure only the tracks that are missing one">
          <input
            type="checkbox"
            checked={tagsOnly}
            onChange={(event) => setTagsOnly(event.target.checked)}
          />
          Only measure what the tags lack
        </label>
        <button
          type="button"
          className="ghost small"
          onClick={() => setCompact((value) => !value)}
          title="One line per file, or the full card for each"
        >
          {compact ? 'Verbose' : 'Normal'}
        </button>
        <div className="actions">
          <button type="button" className="ghost" onClick={exportJson} disabled={done === 0}>
            Download JSON
          </button>
          <button
            type="button"
            className="ghost"
            onClick={() => void downloadCovers()}
            disabled={items.length === 0 || savingSnapshot}
            title="Every album cover the files carry, in one zip laid out like the card"
          >
            Download covers
          </button>
          {cardHandle && (
            <button
              type="button"
              className="primary"
              onClick={() => void saveToCard()}
              disabled={!canSave || savingSnapshot}
              title="Write xtape.db at the card's root, in the folder opened at intake."
            >
              {savingSnapshot ? 'Writing…' : 'Save to card'}
            </button>
          )}
          <button
            type="button"
            className="primary"
            onClick={() => void sendToPlayer()}
            disabled={!canSave || savingSnapshot}
            title="Post the measurements to the player over the network; it writes them to its own flash."
          >
            {savingSnapshot ? 'Sending…' : 'Send to player'}
          </button>
          <button
            type="button"
            className="ghost"
            onClick={() => void saveSnapshotFile()}
            disabled={!canSave || savingSnapshot}
            title="Download xtape.db and move it to the card's root yourself"
          >
            Download .db
          </button>
          <button
            type="button"
            className="ghost"
            onClick={() => void clearAll()}
            disabled={busy || items.length === 0}
          >
            Clear
          </button>
        </div>
      </div>

      {covers && <p className="muted small">{covers}</p>}

      {lastSnapshot && (
        <div className="last-snapshot">
          <span className="muted small">
            Last snapshot: {plural(lastSnapshot.tracks, 'track')} ·{' '}
            {formatBytes(lastSnapshot.bytes.length)} ·{' '}
            {lastSnapshot.sentAt
              ? `accepted ${new Date(lastSnapshot.sentAt).toLocaleTimeString()}`
              : `built ${new Date(lastSnapshot.at).toLocaleTimeString()}`}
          </span>
          <div className="actions">
            <button
              type="button"
              className="ghost small"
              onClick={() => void resendLast()}
              disabled={savingSnapshot}
              title="Send the same image again, without measuring anything"
            >
              Send to player
            </button>
            <button type="button" className="ghost small" onClick={downloadLast}>
              Download
            </button>
            <button type="button" className="ghost small" onClick={() => void discardLast()}>
              Discard
            </button>
          </div>
        </div>
      )}

      <QueueList
        items={items}
        busy={busy}
        compact={compact}
        onDeep={deepAnalyse}
        onRetry={retry}
        onSave={saveTags}
      />

      <footer className="foot muted small">
        <span>Analysis runs locally. Models CC BY-NC 4.0 (MTG/UPF). Code AGPL-3.0.</span>
        {SOURCE_URL && (
          <a href={SOURCE_URL} target="_blank" rel="noreferrer">
            Source
          </a>
        )}
      </footer>
    </div>
  );
}
