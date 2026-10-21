export const ANALYSIS_SCHEMA = 1;

export const MOOD_TAGS = [
  'happy',
  'sad',
  'aggressive',
  'relaxed',
  'party',
  'danceable',
] as const;

export type MoodTag = (typeof MOOD_TAGS)[number];

export type TrackFormat = 'mp3' | 'flac';
export type TempoEngine = 'fast' | 'deep';
export type MeasurementSource = 'metadata' | 'measured';

export interface Analysis {
  schema: typeof ANALYSIS_SCHEMA;
  track: {
    name: string;
    /** Relative path inside the dropped folder; equals name for a single file. */
    path: string;
    size: number;
    format: TrackFormat;
    duration: number;
    hash: string;
  };
  tempo: {
    /** Folded into 70–180 BPM; the value the interface shows. */
    bpm: number;
    /** The detector's output before folding. */
    rawBpm: number;
    /** 0–1 heuristic; not calibrated. Null when the value came from a tag. */
    confidence: number | null;
    /** Null when the value came from a tag. */
    engine: TempoEngine | null;
    escalated: boolean;
    source: MeasurementSource;
  };
  key: {
    tonic: string;
    mode: 'major' | 'minor';
    /** Derived harmonic-mixing code, e.g. 8A. */
    camelot: string;
    /** KeyExtractor's correlation strength. Null when the value came from a tag. */
    strength: number | null;
    source: MeasurementSource;
  };
  mood: {
    /** Six model probabilities, or null when the mood came from a tag. */
    tags: Record<MoodTag, number> | null;
    /** Free-text mood from a tag, e.g. "Happy"; null when measured. */
    label: string | null;
    source: MeasurementSource;
    /** DEAM outputs; null when their model did not run. */
    valence: number | null;
    arousal: number | null;
  };
  /** Arousal presented as energy, or a tagged energy value. */
  energy: number | null;
  energySource: MeasurementSource | null;
  provenance: {
    analyzer: string;
    modelSet: string;
    /** The mood window; null when no mood/energy model ran. */
    window: { start: number; duration: number } | null;
    analysedAt: string;
  };
}

export interface ModelManifest {
  modelSet: string;
  licence: string;
  source: string;
  models: { name: string; sha256: string; bytes: number }[];
}

export interface ExportDocument {
  schema: typeof ANALYSIS_SCHEMA;
  generatedAt: string;
  analyses: Analysis[];
}
