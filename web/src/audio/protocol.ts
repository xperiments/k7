import type { Analysis } from '../domain/analysis';
import type { DecodedAudio, Stage, TrackInput } from './analyzer';
import type { TrackMetadata } from './metadata';

export type WorkerRequest =
  | {
      type: 'analyze';
      id: string;
      track: TrackInput;
      audio: DecodedAudio;
      metadata: TrackMetadata;
    }
  | { type: 'deep'; id: string; audio: DecodedAudio };

export type WorkerResponse =
  | { type: 'stage'; id: string; stage: Stage }
  | { type: 'result'; id: string; analysis: Analysis }
  | { type: 'deep-result'; id: string; tempo: Analysis['tempo'] }
  | { type: 'error'; id: string; message: string; weight?: boolean };
