// Downloads the pinned ONNX model set from essentia.upf.edu into public/models/.
// The files are committed: the App fetches them from jsDelivr at the pinned tag, which
// serves this repository's tree, so `public/models/` is the CDN's source. This script
// re-fetches them for a local checkout (and after a tag bump) and verifies each hash.
// The hashes live in weights.lock.json, which the browser app also reads, so a fetched
// model and the model the app verifies cannot drift apart.
import { createHash } from 'node:crypto';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const out = join(root, 'public', 'models');
const base = 'https://essentia.upf.edu/models';

const lock = JSON.parse(await readFile(join(root, 'weights.lock.json'), 'utf8'));
const models = lock.models.map(({ name, source, sha256, bytes }) => ({
  name,
  path: source,
  sha256,
  bytes,
}));

function modelSetOf(entries) {
  const hash = createHash('sha256')
    .update(entries.map((m) => `${m.name}:${m.sha256}`).sort().join('\n'))
    .digest('hex');
  return `sha256:${hash}`;
}

const modelSet = modelSetOf(models);
if (modelSet !== lock.modelSet) {
  throw new Error(
    `weights.lock.json is inconsistent: modelSet says ${lock.modelSet}, the models hash to ${modelSet}`,
  );
}

async function sha256File(path) {
  return createHash('sha256').update(await readFile(path)).digest('hex');
}

async function exists(path) {
  try {
    return (await stat(path)).size;
  } catch {
    return 0;
  }
}

await mkdir(out, { recursive: true });

for (const model of models) {
  const target = join(out, model.name);
  const size = await exists(target);
  if (size === model.bytes && (await sha256File(target)) === model.sha256) {
    console.log(`cached  ${model.name}`);
    continue;
  }

  process.stdout.write(`fetch   ${model.name} … `);
  const response = await fetch(`${base}/${model.path}`, {
    headers: { 'user-agent': 'xtape-analysis model fetcher' },
    redirect: 'follow',
  });
  if (!response.ok) {
    throw new Error(`${model.name}: HTTP ${response.status}`);
  }
  const bytes = new Uint8Array(await response.arrayBuffer());
  const digest = createHash('sha256').update(bytes).digest('hex');
  if (bytes.byteLength !== model.bytes || digest !== model.sha256) {
    throw new Error(
      `${model.name}: checksum mismatch (got ${bytes.byteLength} bytes / ${digest})`,
    );
  }
  await writeFile(target, bytes);
  console.log('ok');
}

await writeFile(
  join(out, 'manifest.json'),
  `${JSON.stringify(
    {
      modelSet: lock.modelSet,
      licence: lock.licence,
      source: lock.source,
      models: models.map(({ name, sha256, bytes }) => ({ name, sha256, bytes })),
    },
    null,
    2,
  )}\n`,
);

console.log(`model set ${lock.modelSet.slice(7, 23)}… -> public/models`);
