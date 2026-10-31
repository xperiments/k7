import type { Analysis, TrackFormat } from '../domain/analysis';
import type { Stage } from '../audio/analyzer';

export type ItemStatus =
  | 'queued'
  | 'hashing'
  | 'decoding'
  | 'analysing'
  | 'done'
  | 'tagged'
  | 'error';

export interface QueueItem {
  id: string;
  file: File;
  name: string;
  path: string;
  format: TrackFormat;
  status: ItemStatus;
  stage?: Stage;
  analysis?: Analysis;
  cached?: boolean;
  error?: string;
  saving?: boolean;
}
