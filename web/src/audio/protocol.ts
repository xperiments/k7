import type { Analysis } from '../domain/analysis';
import type { DecodedAudio, Stage, TrackInput } from './analyzer';
import type { TrackMetadata } from './metadata';

/** What a worker's finalization produced: the saved file, bytes it could not save, or no change. */
export type FinalizeResult =
  | { kind: 'written'; file: File; hash: string }
  | { kind: 'written'; bytes: Uint8Array; hash: string }
  | { kind: 'complete' };

export type WorkerRequest =
  | {
      type: 'analyze';
      id: string;
      track: TrackInput;
      audio: DecodedAudio;
      metadata: TrackMetadata;
    }
  | { type: 'deep'; id: string; audio: DecodedAudio }
  /** Tags, label and the card write, in the same worker that measured the file. */
  | {
      type: 'finalize';
      id: string;
      file: File;
      analysis: Analysis;
      card?: FileSystemDirectoryHandle;
      path: string;
    }
  /** The boot check: answer without loading a model, so starting a worker is cheap to test. */
  | { type: 'probe'; id: string };

export type WorkerResponse =
  | { type: 'stage'; id: string; stage: Stage }
  | { type: 'result'; id: string; analysis: Analysis }
  | { type: 'deep-result'; id: string; tempo: Analysis['tempo'] }
  /** How long a small fixed workload took; the pool only needs to know it answered. */
  | { type: 'probe-result'; id: string; ms: number }
  | { type: 'finalized'; id: string; result: FinalizeResult }
  | { type: 'error'; id: string; message: string; weight?: boolean };
