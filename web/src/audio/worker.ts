import { loadEssentia } from '../weights/essentia';
import { WeightError } from '../weights/errors';
import { loadModelBytes } from '../weights/manifest';
import { Analyzer } from './analyzer';
import { browserRuntime } from './browser-runtime';
import { loadManifest } from './models';
import { writeTrackMetadata } from './tagWriter';
import { sha256Hex } from '../lib/sha256';
import { writableCard, writeCardFile } from '../card/write';
import type { WorkerRequest, WorkerResponse } from './protocol';

const post = (message: WorkerResponse, transfer: Transferable[] = []) =>
  (self as unknown as { postMessage: (message: WorkerResponse, transfer?: Transferable[]) => void }).postMessage(
    message,
    transfer,
  );

let analyzerPromise: Promise<Analyzer> | null = null;

function getAnalyzer(): Promise<Analyzer> {
  if (!analyzerPromise) {
    analyzerPromise = (async () => {
      const manifest = await loadManifest();
      const analyzer = new Analyzer(browserRuntime, manifest, loadModelBytes, loadEssentia);
      await analyzer.init();
      return analyzer;
    })().catch((error: unknown) => {
      analyzerPromise = null;
      throw error;
    });
  }
  return analyzerPromise;
}

self.addEventListener('message', (event: MessageEvent<WorkerRequest>) => {
  const message = event.data;

  /* Tags and the label belong to the same lane as the measurement: the file's bytes come
   * from the page, TagLib and the label renderer run here, and the bytes go back for the one
   * write to the card. */
  if (message.type === 'finalize') {
    void (async () => {
      try {
        const result = await writeTrackMetadata(message.file, message.analysis);
        if (result.kind !== 'written') {
          post({ type: 'finalized', id: message.id, result: { kind: 'complete' } });
          return;
        }

        const hash = await sha256Hex(result.bytes);
        if (message.card !== undefined) {
          const card = writableCard(message.card);
          if (card !== null) {
            const fresh = await writeCardFile(card, message.path, result.bytes).catch(
              () => null,
            );
            if (fresh !== null) {
              post({
                type: 'finalized',
                id: message.id,
                result: { kind: 'written', file: fresh, hash },
              });
              return;
            }
          }
        }

        /* The worker could not write through the handle; the page still can. */
        post(
          {
            type: 'finalized',
            id: message.id,
            result: { kind: 'written', bytes: result.bytes, hash },
          },
          [result.bytes.buffer as Transferable],
        );
      } catch (error) {
        post({
          type: 'error',
          id: message.id,
          message: error instanceof Error ? error.message : String(error),
        });
      }
    })();
    return;
  }

  /* The boot probe answers before the analyzer is asked for, so it proves a worker can start
   * and run JavaScript without pulling the models into memory. */
  if (message.type === 'probe') {
    const started = performance.now();
    let work = 0;
    for (let step = 0; step < 200_000; step += 1) work += Math.sqrt(step % 1024);
    void work;
    post({ type: 'probe-result', id: message.id, ms: performance.now() - started });
    return;
  }

  void (async () => {
    try {
      const analyzer = await getAnalyzer();

      if (message.type === 'analyze') {
        const analysis = await analyzer.analyze(
          message.audio,
          message.track,
          message.metadata,
          (stage) =>
          post({ type: 'stage', id: message.id, stage }),
        );
        post({ type: 'result', id: message.id, analysis });
        return;
      }

      const tempo = await analyzer.deepTempoOnly(message.audio);
      post({ type: 'deep-result', id: message.id, tempo });
    } catch (error) {
      post({
        type: 'error',
        id: message.id,
        message: error instanceof Error ? error.message : String(error),
        weight: error instanceof WeightError,
      });
    }
  })();
});
