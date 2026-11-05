import { existsSync, readFileSync } from 'node:fs';
import { copyFile, readdir, readFile, rm, stat, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import react from '@vitejs/plugin-react';
import tailwindcss from '@tailwindcss/vite';
import type { Plugin } from 'vite';
import { defineConfig } from 'vitest/config';
import { patchTaglibGlue } from './src/audio/taglib-glue.ts';

const root = fileURLToPath(new URL('.', import.meta.url));

/**
 * The development origin is `https://k7-dev.xperiments.dev:8000` (k7-deploy.md §4). Its
 * certificate is a local mkcert pair, so it is trusted without a warning and the App is a
 * genuine secure context there — the same shape as production, which is what Chrome's
 * Local Network Access permission requires. Without the pair the servers stay plain HTTP:
 * `npm run dev` still works, but an insecure origin is refused the permission and the
 * player cannot be reached.
 */
const devCertPath = process.env.K7_DEV_CERT ?? resolve(root, '.certs/k7-dev.pem');
const devKeyPath = process.env.K7_DEV_KEY ?? resolve(root, '.certs/k7-dev-key.pem');
const devHttps =
  existsSync(devCertPath) && existsSync(devKeyPath)
    ? { cert: readFileSync(devCertPath), key: readFileSync(devKeyPath) }
    : undefined;
/* MCP from the development origin. The device answers `/mcp` on plain HTTP and its guard
 * accepts only a same-origin request whose host is the device's own name, so a page served
 * over https cannot call it: the browser blocks the mixed content, and the request's Origin
 * would not match the device's Host anyway. This proxy makes it a same-origin call to the
 * dev server, which forwards it from Node — no Origin header, Host rewritten to the device's
 * — so neither rule is broken. `K7_DEVICE` points it at another address. */
const mcpProxy = {
  '/mcp': {
    target: process.env.K7_DEVICE ?? 'http://k7.local',
    changeOrigin: true,
    configure: (proxy: { on: (event: 'proxyReq', fn: (req: { removeHeader: (name: string) => void }) => void) => void }) => {
      proxy.on('proxyReq', (req) => req.removeHeader('origin'));
    },
  },
};

const packageVersion = (
  JSON.parse(readFileSync(resolve(root, 'package.json'), 'utf8')) as { version: string }
).version;

/**
 * TagLib's glue is bundled, not fetched as a weight, so the App's Content Security Policy has
 * to be honoured before the bundle exists: its embind code builds invokers with `new Function`
 * (74 of them during `TagLib.initialize()` alone) and the policy refuses every one. The
 * replacement is the closures those bodies produced; `src/audio/taglib-glue.ts` says which
 * three sites and why, and fails the build on glue it does not recognise.
 */
function taglibWithoutDynamicCode(): Plugin {
  return {
    name: 'k7-taglib-without-dynamic-code',
    enforce: 'pre',
    transform(code, id) {
      if (!id.includes('taglib-wasm/dist/taglib-wrapper.js')) return null;
      return { code: patchTaglibGlue(code), map: null };
    },
  };
}

/**
 * A development origin must not carry a service worker (§ pwa.ts), and an old bundle cannot
 * be edited to stop registering one. The browser does it for us: a registration whose script
 * answers 404 is redundant and is discarded on its next update check. The hosted App is
 * built, not served, so nothing in production is affected.
 */
function noWorkerInDevelopment(): Plugin {
  const refuse = (req: { url?: string }, res: { statusCode: number; end: (body: string) => void }, next: () => void): void => {
    if (req.url?.split('?')[0] !== '/sw.js') { next(); return; }
    res.statusCode = 404;
    res.end('a development origin carries no service worker; the hosted App does');
  };
  return {
    name: 'k7-no-worker-in-development',
    apply: 'serve',
    configureServer(server) { server.middlewares.use(refuse); },
    configurePreviewServer(server) { server.middlewares.use(refuse); },
  };
}

/**
 * No `COOP` and no `COEP`, deliberately (ADR 0058).
 *
 * `Cross-Origin-Opener-Policy: same-origin` would make this document cross-origin isolated,
 * and without isolation there is no `SharedArrayBuffer` to protect, so `COEP: require-corp`
 * has nothing to buy — it would only refuse the CDN's weights.
 */

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
 * Remove from the built App what the App origin does not serve: the weights, and nothing
 * else.
 *
 * Every build compiles in CDN mode (`weightsUrl`, `src/weights/config.ts`) and the CDN is
 * the repository at the pinned tag, so the local copies Vite carried out of `public/` must
 * not reach the deployment. `public/` keeps them so `npm run dev` and `npm test` need no
 * network. The ORT glue `.mjs` stays: it is code, not a weight, and `browser-runtime.ts`
 * loads it from `/ort/` on this origin.
 */
async function pruneAppArtifact(dir: string): Promise<number> {
  let bytes = 0;

  const dropDir = async (path: string): Promise<void> => {
    let files: string[];
    try {
      files = await collectFiles(path);
    } catch {
      return;
    }
    for (const file of files) bytes += (await stat(file)).size;
    await rm(path, { recursive: true, force: true });
  };

  const dropFile = async (path: string): Promise<void> => {
    try {
      bytes += (await stat(path)).size;
    } catch {
      return;
    }
    await rm(path, { force: true });
  };

  await dropDir(resolve(dir, 'models'));
  await dropDir(resolve(dir, 'weights'));

  const ort = resolve(dir, 'ort');
  const entries = await readdir(ort).catch(() => [] as string[]);
  for (const entry of entries) {
    if (entry.endsWith('.wasm')) await dropFile(resolve(ort, entry));
  }

  return bytes;
}

/**
 * The App's build outputs: the worker template with its version filled in, the precache
 * list the worker installs from, and the weight copies the artifact does not serve.
 */
function shellOutputs(cdnWeights: boolean): Plugin {
  let emitted: string[] = [];

  return {
    name: 'xtape-shell-outputs',
    apply: 'build',
    generateBundle(_options, bundle) {
      emitted = Object.keys(bundle)
        .filter((file) => !file.endsWith('.map') && !file.endsWith('.wasm'))
        .sort();
    },
    async closeBundle() {
      const outDir = resolve(root, 'dist');

      /* The lock is copied so the page can enumerate what it needs; the worker no longer
       * keys a weights cache on it, because the weights have one offline store and it is
       * IndexedDB (O2). */
      await copyFile(resolve(root, 'weights.lock.json'), resolve(outDir, 'weights.lock.json'));

      // sw.js is a template: its cache names have to be exact in the output, so the values
      // are written here rather than fetched and guessed at install time.
      const sw = await readFile(resolve(root, 'public/sw.js'), 'utf8');
      await writeFile(
        resolve(outDir, 'sw.js'),
        sw.replaceAll('__APP_VERSION__', packageVersion),
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
          '/icons/icon-512-maskable.png',
          /* The brand mark sits in the sidebar on every page: offline it must be there on
           * the first paint, not after a fetch that fails. */
          '/icons/k7logo.svg',
          '/weights.lock.json',
          ...emitted.map((file) => `/${file}`),
        ]),
      );
      await writeFile(
        resolve(outDir, 'shell.json'),
        `${JSON.stringify({ version: packageVersion, assets }, null, 2)}\n`,
      );

      /* The App fetches every weight from the pinned CDN, so the copies under `public/` are
       * not kept: the deployment ships the App, not the CDN's weights. */
      if (cdnWeights) {
        const dropped = await pruneAppArtifact(outDir);
        if (dropped > 0) {
          this.info(`artifact: dropped ${Math.round(dropped / 1024)} KiB the App does not serve`);
        }
      }
    },
  };
}

export default defineConfig(({ mode }) => {
  /* A production build serves the weights from jsDelivr at the pinned tag, whose source is
   * this repository's tree at that tag (`web/public/models/`). The artifact therefore drops
   * the local copies (see `pruneAppArtifact`); `dev` and `test` read the local tree. */
  const cdnWeights = mode === 'production' || process.env.K7_WEIGHTS === 'cdn';

  return {
    plugins: [
      react(),
      tailwindcss(),
      noWorkerInDevelopment(),
      taglibWithoutDynamicCode(),
      shellOutputs(cdnWeights),
    ],
    /* The dev origin is `https://k7-dev.xperiments.dev:8000`; the port is part of it, and
     * the certificate only exists once someone has run mkcert (k7-deploy.md §4). */
    server: { host: true, port: 8000, https: devHttps, proxy: mcpProxy },
    preview: { host: true, port: 8000, https: devHttps, proxy: mcpProxy },
    publicDir: 'public',
    resolve: {
      conditions: ['onnxruntime-web-use-extern-wasm'],
      alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) },
    },
    define: {
      __APP_VERSION__: JSON.stringify(packageVersion),
      __WEIGHTS_MODE__: JSON.stringify(cdnWeights ? 'cdn' : 'local'),
    },
    build: {
      outDir: 'dist',
    },
    worker: { format: 'es' },
    test: {
      environment: 'node',
      include: ['tests/**/*.test.ts', 'src/**/*.test.ts'],
      testTimeout: 120_000,
    },
  };
});
