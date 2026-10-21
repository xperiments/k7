// Downloads the pinned ONNX model set from essentia.upf.edu into public/models/.
// The weights are CC BY-NC 4.0 and are deliberately not committed to this repository.
import { createHash } from 'node:crypto';
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const out = join(root, 'public', 'models');
const base = 'https://essentia.upf.edu/models';

/** @type {{name: string, path: string, sha256: string, bytes: number}[]} */
const models = [
  {
    name: 'discogs-effnet-bsdynamic-1.onnx',
    path: 'feature-extractors/discogs-effnet/discogs-effnet-bsdynamic-1.onnx',
    sha256: 'a280825b334797cf677939db8cd5762c0392aedd0ca6415dbc1cd083f045e43c',
    bytes: 18027718,
  },
  {
    name: 'msd-musicnn-1.onnx',
    path: 'feature-extractors/musicnn/msd-musicnn-1.onnx',
    sha256: '49668ffec47e52e94b96f45930bb46a28a1368d4bdfb5c05378fa834aca616e1',
    bytes: 3168334,
  },
  {
    name: 'mood_happy-discogs-effnet-1.onnx',
    path: 'classification-heads/mood_happy/mood_happy-discogs-effnet-1.onnx',
    sha256: '0ca322819ef137b4b87e9866bffe7370a630e6f1165184ec106326cef6f81e06',
    bytes: 514097,
  },
  {
    name: 'mood_sad-discogs-effnet-1.onnx',
    path: 'classification-heads/mood_sad/mood_sad-discogs-effnet-1.onnx',
    sha256: '1a50d11c23181bdfdeabc7d6032a2dad829dfc19c3776d2ee57ef1724cb08805',
    bytes: 514093,
  },
  {
    name: 'mood_aggressive-discogs-effnet-1.onnx',
    path: 'classification-heads/mood_aggressive/mood_aggressive-discogs-effnet-1.onnx',
    sha256: 'de36550b5d1660791ad732ed6de6ebfdc3e65dcf50b928b2578ddf103dbfb400',
    bytes: 514107,
  },
  {
    name: 'mood_relaxed-discogs-effnet-1.onnx',
    path: 'classification-heads/mood_relaxed/mood_relaxed-discogs-effnet-1.onnx',
    sha256: '8ba6515a1e5943a72b3b475e3a25fc7a2ff04142c3eaa6aa0716fca371efdfff',
    bytes: 514101,
  },
  {
    name: 'mood_party-discogs-effnet-1.onnx',
    path: 'classification-heads/mood_party/mood_party-discogs-effnet-1.onnx',
    sha256: 'c50ac2106ec2f209dd04ad48756582df0e3f3512235310d1a4a3fcc453745f04',
    bytes: 514097,
  },
  {
    name: 'danceability-discogs-effnet-1.onnx',
    path: 'classification-heads/danceability/danceability-discogs-effnet-1.onnx',
    sha256: '9ce9b8c44f1dd5df5ffc124e5d41d67acf254232c1b90c7e057e079ab7cead73',
    bytes: 514101,
  },
  {
    name: 'deam-msd-musicnn-2.onnx',
    path: 'classification-heads/deam/deam-msd-musicnn-2.onnx',
    sha256: '2cb8f33f188d73fcb651f7c065b049959166c7852f454a59c13e66fe2c092bde',
    bytes: 82032,
  },
];

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

const modelSetHash = createHash('sha256')
  .update(models.map((m) => `${m.name}:${m.sha256}`).sort().join('\n'))
  .digest('hex');

await writeFile(
  join(out, 'manifest.json'),
  `${JSON.stringify(
    {
      modelSet: `sha256:${modelSetHash}`,
      licence: 'CC BY-NC 4.0 — models by Music Technology Group, UPF',
      source: 'https://essentia.upf.edu/models.html',
      models: models.map(({ name, sha256, bytes }) => ({ name, sha256, bytes })),
    },
    null,
    2,
  )}\n`,
);

console.log(`model set ${modelSetHash.slice(0, 16)}… -> public/models`);
