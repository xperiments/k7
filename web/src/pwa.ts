/**
 * The offline shell only exists on a secure origin: service workers are unavailable on
 * plain HTTP. On `npm run dev` this is a no-op, which is correct — a worker there would
 * cache the source tree instead of the shell.
 *
 * It returns the registration, or null when there is none, so the caller can wire update
 * detection (`watchForUpdate`) and the update banner to the same worker.
 */
export function registerServiceWorker(): Promise<ServiceWorkerRegistration | null> {
  if (location.protocol !== 'https:') return Promise.resolve(null);
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
