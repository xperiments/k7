/**
 * The offline shell only exists on a secure origin: service workers are unavailable on
 * plain HTTP, and the device serves the app over HTTPS. On `npm run dev` this is a no-op,
 * which is correct — a worker there would cache the source tree instead of the shell.
 */
export function registerServiceWorker(): void {
  if (location.protocol !== 'https:') return;
  if (!('serviceWorker' in navigator)) return;

  window.addEventListener('load', () => {
    void navigator.serviceWorker.register('/sw.js', { scope: '/' }).catch(() => {
      // A device that cannot register a worker still runs the app; the offline shell is
      // an improvement, not a precondition.
    });
  });
}
