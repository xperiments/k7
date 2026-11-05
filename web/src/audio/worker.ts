import { loadEssentia } from '../weights/essentia';
import { WeightError } from '../weights/errors';
import { loadModelBytes } from '../weights/manifest';
import { Analyzer } from './analyzer';
import { browserRuntime } from './browser-runtime';
import { loadManifest } from './models';
import type { WorkerRequest, WorkerResponse } from './protocol';

const post = (message: WorkerResponse) =>
  (self as unknown as { postMessage: (message: WorkerResponse) => void }).postMessage(message);

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
