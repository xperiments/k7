/*
 * The app's service worker, written by hand rather than generated.
 *
 * The two placeholders below are substituted by the build with the app version and the
 * sha256 of weights.lock.json. That makes the weights cache name change whenever the
 * manifest changes, so a model swapped at the same URL is a miss, not a stale hit.
 *
 * It deliberately does not call skipWaiting(): a new worker waits for a reload instead of
 * replacing the page under a running analysis.
 */
const APP_VERSION = '__APP_VERSION__';
const MANIFEST_HASH = '__WEIGHTS_MANIFEST_HASH__';

const SHELL_CACHE = `xtape-shell-${APP_VERSION}`;
const WEIGHTS_CACHE = `xtape-weights-${APP_VERSION}-${MANIFEST_HASH}`;
const SHELL_PREFIX = 'xtape-shell-';
const WEIGHTS_PREFIX = 'xtape-weights-';
const CDN_HOST = 'cdn.jsdelivr.net';

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
          const ours = key.startsWith(SHELL_PREFIX) || key.startsWith(WEIGHTS_PREFIX);
          if (!ours) return undefined;
          if (key === SHELL_CACHE || key === WEIGHTS_CACHE) return undefined;
          return caches.delete(key);
        }),
      );
    })(),
  );
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

async function weightFirst(request) {
  const cache = await caches.open(WEIGHTS_CACHE);
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

  if (url.hostname === CDN_HOST) {
    event.respondWith(weightFirst(request));
    return;
  }

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
