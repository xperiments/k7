/*
 * The app's service worker, written by hand rather than generated.
 *
 * `__APP_VERSION__` is substituted by the build with the package version, so a release
 * opens a new shell cache instead of mixing chunks across versions.
 *
 * The weights are deliberately absent. They live content-addressed in IndexedDB, keyed by
 * URL and sha256 and re-verified on every read (`src/weights/loader.ts`), and the App
 * prefetches them. One offline store for a weight, not two: a model that changes costs one
 * file rather than the whole set, and a failed download never leaves the App with no
 * worker at all.
 *
 * It deliberately does not call skipWaiting() on install: a new worker waits for a reload
 * instead of replacing the page under a running analysis. When the user accepts the update
 * banner the page posts SKIP_WAITING, which is the only thing that activates it early.
 */
const APP_VERSION = '__APP_VERSION__';

const SHELL_CACHE = `xtape-shell-${APP_VERSION}`;
const SHELL_PREFIX = 'xtape-shell-';

function isShellAsset(pathname) {
  return (
    pathname.startsWith('/assets/') ||
    pathname.startsWith('/icons/') ||
    pathname.startsWith('/ort/') ||
    pathname.startsWith('/weights/') ||
    pathname === '/manifest.webmanifest' ||
    pathname === '/weights.lock.json'
  );
}

async function shellAssets() {
  // The build writes this list from the files it actually emitted, so dynamically imported
  // chunks (the worker, TagLib) are precached too, not just what index.html names.
  try {
    const response = await fetch('/shell.json', { cache: 'no-store' });
    if (!response.ok) return ['/', '/index.html'];
    const shell = await response.json();
    const assets = Array.isArray(shell.assets)
      ? shell.assets.filter((asset) => typeof asset === 'string')
      : [];
    return Array.from(new Set(['/', '/index.html', ...assets]));
  } catch {
    return ['/', '/index.html'];
  }
}

async function precacheShell() {
  const cache = await caches.open(SHELL_CACHE);
  const assets = await shellAssets();
  await cache.addAll(assets.map((asset) => new Request(asset, { cache: 'reload' })));
}

self.addEventListener('install', (event) => {
  event.waitUntil(precacheShell());
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    (async () => {
      const keys = await caches.keys();
      await Promise.all(
        keys.map((key) => {
          if (!key.startsWith(SHELL_PREFIX) || key === SHELL_CACHE) return undefined;
          return caches.delete(key);
        }),
      );
    })(),
  );
});

self.addEventListener('message', (event) => {
  if (event.data && event.data.type === 'SKIP_WAITING') self.skipWaiting();
});

async function shellNavigation(request) {
  const cache = await caches.open(SHELL_CACHE);
  try {
    const response = await fetch(request);
    if (response.ok) await cache.put('/index.html', response.clone());
    return response;
  } catch {
    // The device is off: the shell still opens and says so.
    return (await cache.match('/index.html')) ?? Response.error();
  }
}

async function shellAssetFirst(request) {
  const cache = await caches.open(SHELL_CACHE);
  const cached = await cache.match(request);
  if (cached) return cached;
  const response = await fetch(request);
  if (response.ok) await cache.put(request, response.clone());
  return response;
}

self.addEventListener('fetch', (event) => {
  const request = event.request;
  if (request.method !== 'GET') return;

  const url = new URL(request.url);

  // `/api/*` is never cached and never intercepted: the status poll must reach the device.
  if (url.origin === self.location.origin && url.pathname.startsWith('/api/')) return;

  // A cross-origin request is not ours: the weights come from the CDN and are stored by
  // the App in IndexedDB, never here.
  if (url.origin !== self.location.origin) return;

  if (request.mode === 'navigate') {
    // `/stream` and `/library` are the device's own surfaces; the stream must never be
    // mistaken for the shell and written over the cached index.
    if (url.pathname !== '/' && url.pathname !== '/index.html') return;
    event.respondWith(shellNavigation(request));
    return;
  }

  if (isShellAsset(url.pathname)) {
    event.respondWith(shellAssetFirst(request));
  }
});
