import type { ModelManifest } from '../domain/analysis';
import lock from '../../weights.lock.json';
import { WEIGHTS_MODE, type Weight } from './config';
import { loadWeightBytes } from './loader';

interface LockModel {
  name: string;
  source: string;
  sha256: string;
  bytes: number;
  localPath: string;
}

interface LockMedia {
  id: string;
  label: string;
  package: string;
  path: string;
  localPath: string;
  sha256: string;
  bytes: number;
}

interface WeightsLock {
  schema: number;
  modelSet: string;
  licence: string;
  source: string;
  models: LockModel[];
  media: LockMedia[];
}

const data = lock as WeightsLock;

export const MODELS_MISSING_MESSAGE =
  'The ONNX models are missing. Run `npm run models` in web/ and reload.';

/**
 * The manifest is small and is never a weight: it is the list of hashes that makes every
 * other download verifiable, so the shell carries it. The model set hash is the same value
 * written into an analysis's provenance and into the snapshot.
 */
export const MODEL_MANIFEST: ModelManifest = {
  modelSet: data.modelSet,
  licence: data.licence,
  source: data.source,
  models: data.models.map(({ name, sha256, bytes }) => ({ name, sha256, bytes })),
};

function modelWeight(name: string): Weight {
  const model = data.models.find((candidate) => candidate.name === name);
  if (!model) throw new Error(`The weights manifest does not list ${name}.`);

  return {
    id: `model:${model.name}`,
    label: model.name,
    sha256: model.sha256,
    bytes: model.bytes,
    localPath: model.localPath,
    // The models are published beside the app at the pinned tag, so that tag is the
    // whole source; there is no second version to resolve.
    source: { kind: 'gh', path: `web/public/${model.localPath}` },
  };
}

function mediaWeight(id: string): Weight {
  const media = data.media.find((candidate) => candidate.id === id);
  if (!media) throw new Error(`The weights manifest does not list ${id}.`);

  return {
    id: media.id,
    label: media.label,
    sha256: media.sha256,
    bytes: media.bytes,
    localPath: media.localPath,
    source: { kind: 'npm', package: media.package, path: media.path },
  };
}

export const ESSENTIA_WASM = mediaWeight('essentia-wasm');
export const ORT_WASM = mediaWeight('ort-wasm');
export const TAGLIB_WASM = mediaWeight('taglib-web-wasm');

export async function loadModelBytes(name: string): Promise<Uint8Array> {
  return loadWeightBytes(modelWeight(name));
}

/**
 * In a local build the manifest comes from the fetched model directory, so a checkout
 * without weights says so instead of pretending the models are present. In the shell the
 * embedded manifest is the truth: the device does not carry the model files at all.
 */
export async function loadManifest(): Promise<ModelManifest> {
  if (WEIGHTS_MODE === 'cdn') return MODEL_MANIFEST;

  try {
    const response = await fetch('/models/manifest.json', { cache: 'no-store' });
    if (!response.ok) throw new Error(MODELS_MISSING_MESSAGE);
    return (await response.json()) as ModelManifest;
  } catch {
    throw new Error(MODELS_MISSING_MESSAGE);
  }
}
