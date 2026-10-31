/**
 * Where the weights come from.
 *
 * The shell is packed into the device's 1 MB partition; everything larger is fetched.
 * The repository and the tag are the only two values that move when the app is released,
 * because a pinned URL is also the only thing that makes a cache entry reproducible.
 *
 * The model files live beside the app in the repository at `web/public/models`, so the
 * pinned tag must contain them. The runtimes are npm packages and jsDelivr serves them
 * from their published versions, which are pinned here rather than in package.json: a
 * caret range in package.json is a build input, while this table is what the cache was
 * verified against.
 */
export const WEIGHTS_REPO = 'xperiments/xtape';
export const WEIGHTS_REF = 'v0.4.0';

export const PACKAGE_VERSIONS: Record<string, string> = {
  'essentia.js': '0.1.3',
  'onnxruntime-web': '1.30.0',
  'taglib-wasm': '2.4.0',
};

export const CDN_ORIGIN = 'https://cdn.jsdelivr.net';

export type WeightsMode = 'local' | 'cdn';

declare const __WEIGHTS_MODE__: string | undefined;

/**
 * `npm run build:shell` compiles for the device (mode `shell`); every other build serves
 * the weights from the local public tree, so `npm run dev` and `npm test` need no network.
 */
export const WEIGHTS_MODE: WeightsMode =
  typeof __WEIGHTS_MODE__ === 'string'
    ? __WEIGHTS_MODE__ === 'cdn'
      ? 'cdn'
      : 'local'
    : 'local';

export type WeightSource =
  | { kind: 'gh'; path: string }
  | { kind: 'npm'; package: string; path: string };

export interface Weight {
  /** Stable name used by the app, e.g. `essentia-wasm`. */
  id: string;
  /** Human name for a refusal message. */
  label: string;
  sha256: string;
  bytes: number;
  /** Path below the origin in a local build, e.g. `models/foo.onnx`. */
  localPath: string;
  source: WeightSource;
}

/** A GitHub path served by jsDelivr, with leading slashes removed. */
export function cdnGhUrl(path: string): string {
  return `${CDN_ORIGIN}/gh/${WEIGHTS_REPO}@${WEIGHTS_REF}/${path.replace(/^\/+/, '')}`;
}

/** An npm file served by jsDelivr at the version pinned above. */
export function cdnNpmUrl(packageName: string, path: string): string {
  const version = PACKAGE_VERSIONS[packageName];
  if (!version) throw new Error(`No pinned version for ${packageName}`);
  return `${CDN_ORIGIN}/npm/${packageName}@${version}/${path.replace(/^\/+/, '')}`;
}

/** The one place a weight's URL is decided. */
export function weightsUrl(weight: Weight, mode: WeightsMode = WEIGHTS_MODE): string {
  if (mode === 'local') return `/${weight.localPath.replace(/^\/+/, '')}`;

  return weight.source.kind === 'gh'
    ? cdnGhUrl(weight.source.path)
    : cdnNpmUrl(weight.source.package, weight.source.path);
}
