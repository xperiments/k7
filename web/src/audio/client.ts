import type { Analysis } from '../domain/analysis';
import type { DecodedAudio, Stage, TrackInput } from './analyzer';
import type { TrackMetadata } from './metadata';
import type { WorkerRequest, WorkerResponse } from './protocol';

type Resolved = Analysis | Analysis['tempo'];

interface Pending {
  resolve: (value: Resolved) => void;
  reject: (error: Error) => void;
  onStage?: (stage: Stage) => void;
}

export class AnalysisClient {
  private readonly worker: Worker;
  private sequence = 0;
  private readonly pending = new Map<string, Pending>();

  constructor() {
    this.worker = new Worker(new URL('./worker.ts', import.meta.url), { type: 'module' });
    this.worker.addEventListener('message', (event: MessageEvent<WorkerResponse>) => {
      this.handle(event.data);
    });
    this.worker.addEventListener('error', (event) => {
      this.failAll(event.message || 'The analysis worker failed to start.');
    });
  }

  analyze(
    track: TrackInput,
    audio: DecodedAudio,
    metadata: TrackMetadata,
    onStage?: (stage: Stage) => void,
  ): Promise<Analysis> {
    const id = String(++this.sequence);
    return new Promise<Analysis>((resolve, reject) => {
      this.pending.set(id, {
        resolve: resolve as Pending['resolve'],
        reject,
        onStage,
      });
      const message: WorkerRequest = { type: 'analyze', id, track, audio, metadata };
      const transfer: Transferable[] = [audio.pcm16k.buffer as Transferable];
      if (audio.pcm44k) transfer.push(audio.pcm44k.buffer as Transferable);
      this.worker.postMessage(message, transfer);
    });
  }

  deep(audio: DecodedAudio): Promise<Analysis['tempo']> {
    const pcm44k = audio.pcm44k;
    if (!pcm44k) {
      return Promise.reject(new Error('44.1 kHz audio is required for deep analysis'));
    }
    const id = String(++this.sequence);
    return new Promise<Analysis['tempo']>((resolve, reject) => {
      this.pending.set(id, {
        resolve: resolve as Pending['resolve'],
        reject,
      });
      const message: WorkerRequest = { type: 'deep', id, audio };
      this.worker.postMessage(message, [pcm44k.buffer as Transferable]);
    });
  }

  private handle(message: WorkerResponse): void {
    const pending = this.pending.get(message.id);
    if (!pending) return;

    if (message.type === 'stage') {
      pending.onStage?.(message.stage);
      return;
    }

    this.pending.delete(message.id);
    if (message.type === 'error') {
      const error = new Error(message.message);
      // The class does not survive postMessage; the name carries the distinction the
      // Prepare view needs, and the message remains the text shown on the page.
      if (message.weight) error.name = 'WeightError';
      pending.reject(error);
    } else if (message.type === 'result') {
      pending.resolve(message.analysis);
    } else {
      pending.resolve(message.tempo);
    }
  }

  private failAll(message: string): void {
    for (const [id, pending] of this.pending) {
      this.pending.delete(id);
      pending.reject(new Error(message));
    }
  }
}
