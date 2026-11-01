import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { copyFile, mkdir, readdir, readFile, unlink, writeFile } from 'node:fs/promises';
import { basename, extname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';
import type { Plugin } from 'vite';
import { defineConfig } from 'vitest/config';

const root = fileURLToPath(new URL('.', import.meta.url));
const packageVersion = (
  JSON.parse(readFileSync(resolve(root, 'package.json'), 'utf8')) as { version: string }
).version;

const isolationHeaders = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
};

/**
 * Extensions worth compressing: all of them are text, and text shrinks about three to
 * one. Anything already compressed (the PNG icons) is left alone, because gzip would
 * only add bytes.
 */
const COMPRESSIBLE = new Set([
  '.css',
  '.html',
  '.js',
  '.json',
  '.mjs',
  '.pem',
  '.svg',
  '.txt',
  '.wasm',
  '.webmanifest',
]);

async function collectFiles(dir: string): Promise<string[]> {
  const found: string[] = [];
  for (const entry of await readdir(dir, { withFileTypes: true })) {
    const path = resolve(dir, entry.name);
    if (entry.isDirectory()) found.push(...(await collectFiles(path)));
    else found.push(path);
  }
  return found;
}

/**
 * Pack the shell compressed: `<file>.gz` written, the original dropped.
 *
 * The partition is flash, so the bytes in `dist-shell` are the bytes on the board, and
 * the app is 928 KiB of mostly JavaScript. The device never inflates anything - it sends
 * the stored bytes with `Content-Encoding: gzip` (`front_door.c`) and the browser does
 * the work. `index.html` stays plain: the firmware's shell check looks for that exact
 * file, and it is under a kilobyte.
 */
async function precompress(dir: string): Promise<{ raw: number; packed: number }> {
  let raw = 0;
  let packed = 0;
  for (const file of await collectFiles(dir)) {
    if (basename(file) === 'index.html') continue;
    if (!COMPRESSIBLE.has(extname(file))) continue;
    const bytes = await readFile(file);
    const gz = gzipSync(bytes, { level: 9 });
    // Incompressible content keeps its original name: it would be sent plain either way,
    // and two copies of it in flash would be worse than one.
    if (gz.length >= bytes.length) continue;
    await writeFile(`${file}.gz`, gz);
    await unlink(file);
    raw += bytes.length;
    packed += gz.length;
  }
  return { raw, packed };
}

/**
 * The device build. `npm run build` stays the local build with `public/models` and
 * `public/ort` beside it; `npm run build:shell` is the shell packed into the frontdoor
 * partition, with its text files pre-compressed. The public directory is not copied
 * wholesale for the shell, because 38 MB of weights and a 14 MB ORT binary must not be
 * mistaken for app code.
 */
function shellOutputs(shell: boolean): Plugin {
  let emitted: string[] = [];

  return {
    name: 'xtape-shell-outputs',
    apply: 'build',
    generateBundle(_options, bundle) {
      if (shell) {
        // TagLib's loader still contains a fallback URL for its own wasm. The app always
        // hands it verified bytes, so the emitted fallback asset is dead weight.
        for (const file of Object.keys(bundle)) {
          if (file.endsWith('.wasm')) delete bundle[file];
        }
      }

      emitted = Object.keys(bundle)
        .filter((file) => !file.endsWith('.map') && !file.endsWith('.wasm'))
        .sort();
    },
    async closeBundle() {
      const outDir = resolve(root, shell ? 'dist-shell' : 'dist');

      if (shell) {
        await mkdir(resolve(outDir, 'icons'), { recursive: true });
        await mkdir(resolve(outDir, 'ort'), { recursive: true });
        await copyFile(
          resolve(root, 'public/manifest.webmanifest'),
          resolve(outDir, 'manifest.webmanifest'),
        );
        for (const icon of ['icon-192.png', 'icon-512.png']) {
          await copyFile(
            resolve(root, 'public/icons', icon),
            resolve(outDir, 'icons', icon),
          );
        }
        await copyFile(
          resolve(root, 'public/ort/ort-wasm-simd-threaded.mjs'),
          resolve(outDir, 'ort/ort-wasm-simd-threaded.mjs'),
        );
      }

      const lock = await readFile(resolve(root, 'weights.lock.json'));
      await copyFile(resolve(root, 'weights.lock.json'), resolve(outDir, 'weights.lock.json'));
      const manifestHash = createHash('sha256').update(lock).digest('hex');

      // sw.js is a template: its cache names have to be exact in the output, so the values
      // are written here rather than fetched and guessed at install time.
      const sw = await readFile(resolve(root, 'public/sw.js'), 'utf8');
      await writeFile(
        resolve(outDir, 'sw.js'),
        sw
          .replaceAll('__APP_VERSION__', packageVersion)
          .replaceAll('__WEIGHTS_MANIFEST_HASH__', manifestHash),
      );

      // The worker precaches this list at install. It is generated from the files that
      // were actually emitted, so dynamic chunks are included and a missing one is caught
      // by the install rather than by an offline reload.
      const assets = Array.from(
        new Set([
          '/index.html',
          '/manifest.webmanifest',
          '/icons/icon-192.png',
          '/icons/icon-512.png',
          '/weights.lock.json',
          ...(shell ? ['/ort/ort-wasm-simd-threaded.mjs'] : []),
          ...emitted.map((file) => `/${file}`),
        ]),
      );
      await writeFile(
        resolve(outDir, 'shell.json'),
        `${JSON.stringify({ version: packageVersion, manifestHash, assets }, null, 2)}\n`,
      );

      if (shell) {
        const { raw, packed } = await precompress(outDir);
        this.info(
          `shell: ${raw} -> ${packed} bytes on flash (gzip, ` +
            `${Math.round((1 - packed / raw) * 100)}% smaller)`,
        );
      }
    },
  };
}

export default defineConfig(({ mode }) => {
  const shell = mode === 'shell';

  return {
    plugins: [react(), tailwindcss(), shellOutputs(shell)],
    publicDir: shell ? false : 'public',
    resolve: {
      conditions: ['onnxruntime-web-use-extern-wasm'],
      alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) },
    },
    define: {
      __APP_VERSION__: JSON.stringify(packageVersion),
      __WEIGHTS_MODE__: JSON.stringify(shell ? 'cdn' : 'local'),
    },
    build: {
      outDir: shell ? 'dist-shell' : 'dist',
    },
    server: { headers: isolationHeaders },
    preview: { headers: isolationHeaders },
    worker: { format: 'es' },
    test: {
      environment: 'node',
      include: ['tests/**/*.test.ts', 'src/**/*.test.ts'],
      testTimeout: 120_000,
    },
  };
});
