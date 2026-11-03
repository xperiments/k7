/**
 * The offline shell is a property of the **hosted** App: it precaches the built shell so a
 * reload works with no network. Two origins must never have one.
 *
 * Plain HTTP cannot. And a **development origin** must not: its assets change with every
 * edit while the worker's cached `index.html` keeps naming the old hashes, so the next load
 * asks for a file that no longer exists and the stylesheet dies with `net::ERR_FAILED`. The
 * hosted App is always port 443, so a port is the honest test for "this is not it".
 *
 * Leaving a worker behind on a development origin is worse than never registering one, so
 * the opt-out also removes the one an earlier build installed.
 *
 * It returns the registration, or null when there is none, so the caller can wire update
 * detection (`watchForUpdate`) and the update banner to the same worker.
 */
export function registerServiceWorker(): Promise<ServiceWorkerRegistration | null> {
  if (location.protocol !== 'https:') return Promise.resolve(null);
  if (location.port !== '') {
    void discardDevelopmentWorker();
    return Promise.resolve(null);
  }
  if (!('serviceWorker' in navigator)) return Promise.resolve(null);

  return new Promise((resolve) => {
    const register = (): void => {
      try {
        void navigator.serviceWorker
          .register('/sw.js', { scope: '/' })
          .then((registration) => resolve(registration))
          .catch(() => {
            // A device that cannot register a worker still runs the app; the offline shell
            // is an improvement, not a precondition.
            resolve(null);
          });
      } catch {
        // A browser that refuses the call outright is in the same place as one that
        // rejects it: no worker, and the app still runs.
        resolve(null);
      }
    };

    // Registering after `load` keeps the worker out of the first paint's critical path.
    // A call that arrives late must not wait for an event that already happened.
    if (document.readyState === 'complete') register();
    else window.addEventListener('load', register, { once: true });
  });
}

/**
 * Undo what an earlier build left on a development origin: the registrations, and the shell
 * caches they filled. Best effort — a page that cannot clean up still runs.
 */
async function discardDevelopmentWorker(): Promise<void> {
  try {
    const registrations = await navigator.serviceWorker?.getRegistrations?.();
    await Promise.all((registrations ?? []).map((registration) => registration.unregister()));
    if (typeof caches === 'undefined') return;
    const names = await caches.keys();
    await Promise.all(
      names.filter((name) => name.startsWith('k7-shell-')).map((name) => caches.delete(name)),
    );
  } catch {
    /* Nothing to clean, or no permission to: either way the page still works. */
  }
}
