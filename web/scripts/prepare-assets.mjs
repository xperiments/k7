// Copies the runtime files a local build serves itself, so `npm run dev` and `npm test`
// need no network. Every copied binary is checked against weights.lock.json, so a
// node_modules version that no longer matches the pin fails here instead of in the worker
// at analysis time. A production build fetches the binaries from jsDelivr and its artifact
// drops these local copies (`pruneAppArtifact`).
import { createHash } from 'node:crypto';
import { copyFile, mkdir, readFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const modules = join(root, 'node_modules');

const lock = JSON.parse(await readFile(join(root, 'weights.lock.json'), 'utf8'));
const pinned = new Map(lock.media.map((entry) => [entry.id, entry]));

async function copyWeight(source, target, id) {
  await copyFile(source, target);
  const entry = pinned.get(id);
  if (!entry) throw new Error(`weights.lock.json does not list ${id}`);
  const digest = createHash('sha256').update(await readFile(target)).digest('hex');
  if (digest !== entry.sha256) {
    throw new Error(
      `${id}: ${source} does not match weights.lock.json (got ${digest}). ` +
        'Update the lock or reinstall the pinned package version.',
    );
  }
}

const ort = join(modules, 'onnxruntime-web', 'dist');
const ortOut = join(root, 'public', 'ort');
const weightsOut = join(root, 'public', 'weights');

await mkdir(ortOut, { recursive: true });
await mkdir(weightsOut, { recursive: true });

// ORT's loader glue has no hash of its own: it is part of the code, not a weight.
await copyFile(
  join(ort, 'ort-wasm-simd-threaded.mjs'),
  join(ortOut, 'ort-wasm-simd-threaded.mjs'),
);
await copyWeight(
  join(ort, 'ort-wasm-simd-threaded.wasm'),
  join(ortOut, 'ort-wasm-simd-threaded.wasm'),
  'ort-wasm',
);
await copyWeight(
  join(modules, 'essentia.js', 'dist', 'essentia-wasm.es.js'),
  join(weightsOut, 'essentia-wasm.es.js'),
  'essentia-wasm',
);
await copyWeight(
  join(modules, 'taglib-wasm', 'dist', 'taglib-web.wasm'),
  join(weightsOut, 'taglib-web.wasm'),
  'taglib-web-wasm',
);

console.log('runtime assets -> public/ort, public/weights');
