import type { Analysis } from '../src/domain/analysis';

export const sampleAnalysis: Analysis = {
  schema: 1,
  track: {
    name: 'sample.mp3',
    path: 'sample.mp3',
    size: 1234,
    format: 'mp3',
    duration: 8,
    hash: 'sample-hash',
  },
  tempo: {
    bpm: 128.4,
    rawBpm: 128.4,
    confidence: 0.9,
    engine: 'fast',
    escalated: false,
    source: 'measured',
  },
  key: {
    tonic: 'A',
    mode: 'minor',
    camelot: '8A',
    strength: 0.75,
    source: 'measured',
  },
  mood: {
    tags: {
      happy: 0.1,
      sad: 0.2,
      aggressive: 0.05,
      relaxed: 0.7,
      party: 0.1,
      danceable: 0.5,
    },
    label: null,
    source: 'measured',
    valence: 0.4,
    arousal: 0.6,
  },
  energy: 0.6,
  energySource: 'measured',
  provenance: {
    analyzer: 'test',
    modelSet: 'sha256:test',
    window: { start: 0, duration: 8 },
    analysedAt: '2026-01-01T00:00:00.000Z',
  },
};
