export { loadManifest, MODELS_MISSING_MESSAGE } from '../weights/manifest';

export const MODEL_FILES = {
  effnet: 'discogs-effnet-bsdynamic-1.onnx',
  musicnn: 'msd-musicnn-1.onnx',
  deam: 'deam-msd-musicnn-2.onnx',
  heads: {
    happy: 'mood_happy-discogs-effnet-1.onnx',
    sad: 'mood_sad-discogs-effnet-1.onnx',
    aggressive: 'mood_aggressive-discogs-effnet-1.onnx',
    relaxed: 'mood_relaxed-discogs-effnet-1.onnx',
    party: 'mood_party-discogs-effnet-1.onnx',
    danceable: 'danceability-discogs-effnet-1.onnx',
  },
} as const;
