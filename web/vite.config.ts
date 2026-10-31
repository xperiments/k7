import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { copyFile, mkdir, readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import react from '@vitejs/plugin-react';
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
 * The device build. `npm run build` stays the local build with `public/models` and
 * `public/ort` beside it; `npm run build:shell` is the shell that fits the 1 MB partition.
 * The public directory is not copied wholesale for the shell, because 38 MB of weights and
 * a 14 MB ORT binary must not be mistaken for app code.
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
        /* The plain listener's page: the certificate to trust and how, for the three
         * systems that open this app. Served over HTTP, so it has to work before trust. */
        await copyFile(resolve(root, 'public/setup.html'), resolve(outDir, 'setup.html'));
        await copyFile(resolve(root, '..', 'tls', 'ca.pem'), resolve(outDir, 'ca.pem'));
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
    },
  };
}

export default defineConfig(({ mode }) => {
  const shell = mode === 'shell';

  return {
    plugins: [react(), shellOutputs(shell)],
    publicDir: shell ? false : 'public',
    resolve: { conditions: ['onnxruntime-web-use-extern-wasm'] },
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
      include: ['tests/**/*.test.ts'],
      testTimeout: 120_000,
    },
  };
});
