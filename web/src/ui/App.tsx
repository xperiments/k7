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
import { SOURCE_URL, cacheKey } from '../config';
import { clearCache, getCached, putCached } from '../storage/cache';
import { DropZone } from './DropZone';
import type { FoundFile } from './files';
import { plural, shortHash } from './format';
import { QueueList } from './QueueList';
import { buildSnapshotExport } from './snapshotExport';
import { writeSnapshotToCard, type SnapshotDirectory } from './cardWriter';
import type { QueueItem } from './queue';

export function App() {
  const [items, setItems] = useState<QueueItem[]>([]);
  const [busy, setBusy] = useState(false);
  const [manifest, setManifest] = useState<ModelManifest | null>(null);
  const modelSet = manifest?.modelSet ?? null;
  const [savingSnapshot, setSavingSnapshot] = useState(false);
  /* The card the user opened read-write, when they opened one: the snapshot goes back
   * there without asking again. A dropped folder leaves this null. */
  const [cardHandle, setCardHandle] = useState<SnapshotDirectory | null>(null);
  const [setupError, setSetupError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [client] = useState(() => new AnalysisClient());
  const started = useRef(false);

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
    async (item: QueueItem) => {
      if (!modelSet) return;
      setBusy(true);
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
        update(item.id, {
          status: 'error',
          error: error instanceof Error ? error.message : String(error),
        });
      } finally {
        started.current = false;
        setBusy(false);
      }
    },
    [client, modelSet, update],
  );

  useEffect(() => {
    if (!modelSet || busy || started.current) return;
    const next = items.find((item) => item.status === 'queued');
    if (!next) return;
    started.current = true;
    void processItem(next);
  }, [items, busy, modelSet, processItem]);

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
        id: crypto.randomUUID(),
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
      started.current = true;
      try {
        update(item.id, { status: 'analysing', stage: 'deep' });
        const buffer = await item.file.arrayBuffer();
        const audio = await decodeFile(buffer);
        const tempo = await client.deep(audio);
        const analysis = { ...item.analysis, tempo };
        await putCached(cacheKey(item.analysis.track.hash, modelSet), analysis);
        update(item.id, { status: 'done', analysis, stage: undefined, cached: false });
      } catch (error) {
        setNotice(
          `Deep analysis failed: ${error instanceof Error ? error.message : String(error)}`,
        );
        update(item.id, { status: 'done', stage: undefined });
      } finally {
        started.current = false;
        setBusy(false);
      }
    },
    [busy, client, modelSet, update],
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
        setNotice(
          `Could not write tags: ${error instanceof Error ? error.message : String(error)}`,
        );
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

  const saveSnapshotFile = useCallback(async () => {
    if (savingSnapshot) return;
    setSavingSnapshot(true);
    try {
      const result = await buildImage();
      if (!result) return;
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
              Fetch the weights once: <code>cd web &amp;&amp; npm run models</code>
            </p>
          </div>
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
        <div className="actions">
          <button type="button" className="ghost" onClick={exportJson} disabled={done === 0}>
            Download JSON
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

      <QueueList
        items={items}
        busy={busy}
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
