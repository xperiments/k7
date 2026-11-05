import type { Analysis } from '../domain/analysis';
import type { DecodedAudio, Stage, TrackInput } from './analyzer';
import type { TrackMetadata } from './metadata';
import type { FinalizeResult, WorkerRequest, WorkerResponse } from './protocol';

type Resolved = Analysis | Analysis['tempo'];

interface Pending {
  resolve: (value: Resolved) => void;
  reject: (error: Error) => void;
  onStage?: (stage: Stage) => void;
}

interface Probe {
  resolve: (ms: number) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}

interface Finalizing {
  resolve: (result: FinalizeResult) => void;
  reject: (error: Error) => void;
}

export class AnalysisClient {
  private readonly worker: Worker;
  private sequence = 0;
  private readonly pending = new Map<string, Pending>();
  private readonly probes = new Map<string, Probe>();
  private readonly finalizing = new Map<string, Finalizing>();

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

  /**
   * Ask the worker to run a small fixed workload and answer. This is the boot check: it
   * proves the worker script starts and can post back, without loading the models the real
   * analysis will need. A worker that does not answer in time is disposed by the caller.
   */
  probe(timeoutMs = 3000): Promise<number> {
    const id = String(++this.sequence);
    return new Promise<number>((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.probes.delete(id)) {
          reject(new Error('The analysis worker did not answer its start check.'));
        }
      }, timeoutMs);
      this.probes.set(id, {
        resolve: (ms) => {
          clearTimeout(timer);
          resolve(ms);
        },
        reject: (error) => {
          clearTimeout(timer);
          reject(error);
        },
        timer,
      });
      const message: WorkerRequest = { type: 'probe', id };
      this.worker.postMessage(message);
    });
  }

  /**
   * Ask the same worker that measured the file to write the tags and the label. The page
   * gets bytes back and saves them; a worker holds this lane until that save is done.
   */
  finalize(
    file: File,
    analysis: Analysis,
    card?: FileSystemDirectoryHandle,
    path = '',
  ): Promise<FinalizeResult> {
    const id = String(++this.sequence);
    return new Promise<FinalizeResult>((resolve, reject) => {
      this.finalizing.set(id, { resolve, reject });
      const message: WorkerRequest = { type: 'finalize', id, file, analysis, card, path };
      try {
        this.worker.postMessage(message);
      } catch {
        /* A handle the platform will not clone is not a reason to lose the tags: finalize
         * without it and let the page write the returned bytes. */
        const { card: _card, ...rest } = message;
        this.worker.postMessage({ ...rest, card: undefined });
      }
    });
  }

  /** Terminate the worker; anything the page was still waiting for fails. */
  dispose(): void {
    this.worker.terminate();
    this.failAll('The analysis worker was stopped.');
  }

  private handle(message: WorkerResponse): void {
    if (message.type === 'probe-result') {
      const probe = this.probes.get(message.id);
      if (!probe) return;
      this.probes.delete(message.id);
      probe.resolve(message.ms);
      return;
    }

    if (message.type === 'finalized') {
      const finalizing = this.finalizing.get(message.id);
      if (!finalizing) return;
      this.finalizing.delete(message.id);
      finalizing.resolve(message.result);
      return;
    }

    if (message.type === 'error') {
      const finalizing = this.finalizing.get(message.id);
      if (finalizing) {
        this.finalizing.delete(message.id);
        finalizing.reject(new Error(message.message));
        return;
      }
    }

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
    for (const probe of this.probes.values()) {
      clearTimeout(probe.timer);
      probe.reject(new Error(message));
    }
    this.probes.clear();
    for (const finalizing of this.finalizing.values()) {
      finalizing.reject(new Error(message));
    }
    this.finalizing.clear();
    for (const [id, pending] of this.pending) {
      this.pending.delete(id);
      pending.reject(new Error(message));
    }
  }
}
