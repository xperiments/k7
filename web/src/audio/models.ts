import type { ModelManifest } from '../domain/analysis';

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

export const MODELS_MISSING_MESSAGE =
  'The ONNX models are missing. Run `npm run models` in web/ and reload.';

export async function loadManifest(baseUrl = '/models'): Promise<ModelManifest> {
  const response = await fetch(`${baseUrl}/manifest.json`, { cache: 'no-store' });
  if (!response.ok) throw new Error(MODELS_MISSING_MESSAGE);
  return (await response.json()) as ModelManifest;
}
