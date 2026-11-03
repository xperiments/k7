import type { Analysis, TrackFormat } from '../domain/analysis';
import type { Stage } from '../audio/analyzer';
import type { TrackTags } from '../audio/metadata';

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
  /** What the file's own tags say; read once per Track, before the cache is consulted. */
  tags?: TrackTags;
  analysis?: Analysis;
  cached?: boolean;
  /** Recognised from the card's own record: not read, not measured again. */
  skipped?: boolean;
  error?: string;
  saving?: boolean;
}
