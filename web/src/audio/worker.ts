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
      const manifest = await loadManifest('/models');
      const analyzer = new Analyzer(browserRuntime, manifest);
      await analyzer.init();
      return analyzer;
    })();
  }
  return analyzerPromise;
}

self.addEventListener('message', (event: MessageEvent<WorkerRequest>) => {
  const message = event.data;

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
      });
    }
  })();
});
