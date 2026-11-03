/**
 * When a new version is ready, say so; then let the user take it.
 *
 * The worker never calls `skipWaiting` itself (O1): it would replace the page under a
 * running analysis. A waiting worker is the signal for the banner (O6), and the banner's
 * action posts `SKIP_WAITING` and reloads once the new worker controls the page.
 *
 * The seams are structural so tests can drive a registration without a browser; a real
 * `ServiceWorkerRegistration` satisfies them.
 */

export interface WorkerLike {
  readonly state: string;
  postMessage(message: unknown): void;
  addEventListener(type: 'statechange', listener: () => void): void;
  removeEventListener(type: 'statechange', listener: () => void): void;
}

export interface RegistrationLike {
  readonly waiting: WorkerLike | null;
  readonly installing: WorkerLike | null;
  addEventListener(type: 'updatefound', listener: () => void): void;
  removeEventListener(type: 'updatefound', listener: () => void): void;
}

export interface ServiceWorkerContainerLike {
  addEventListener(type: 'controllerchange', listener: () => void): void;
  removeEventListener(type: 'controllerchange', listener: () => void): void;
}

export interface UpdateOptions {
  /** Injected for tests; defaults to `navigator.serviceWorker`. */
  serviceWorker?: ServiceWorkerContainerLike;
  /** Injected for tests; defaults to reloading the page. */
  reload?: () => void;
}

const applying = new WeakSet<RegistrationLike>();

function browserServiceWorker(): ServiceWorkerContainerLike | undefined {
  if (typeof navigator === 'undefined') return undefined;
  return navigator.serviceWorker as ServiceWorkerContainerLike | undefined;
}

function reloadPage(): void {
  if (typeof location !== 'undefined') location.reload();
}

/**
 * Calls `onReady` exactly once, when `registration.waiting` exists: immediately if a
 * worker is already waiting, otherwise after `updatefound` and the installing worker's
 * `installed` state. Returns a stop function that removes every listener.
 *
 * A first install also passes through `installing` → `installed`, but it activates rather
 * than waits, so no `waiting` worker appears and nothing is announced.
 */
export function watchForUpdate(
  registration: RegistrationLike,
  onReady: () => void,
): () => void {
  let active = true;
  let ready = false;
  let watchedInstalling: WorkerLike | null = null;

  const releaseInstalling = (): void => {
    if (!watchedInstalling) return;
    watchedInstalling.removeEventListener('statechange', onStateChange);
    watchedInstalling = null;
  };

  const notify = (): void => {
    if (!active || ready || !registration.waiting) return;
    ready = true;
    releaseInstalling();
    registration.removeEventListener('updatefound', onUpdateFound);
    onReady();
  };

  const onStateChange = (): void => {
    notify();
  };

  const onUpdateFound = (): void => {
    if (!active) return;
    if (registration.waiting) {
      notify();
      return;
    }

    const installing = registration.installing;
    if (!installing) return;

    releaseInstalling();
    watchedInstalling = installing;
    installing.addEventListener('statechange', onStateChange);
    if (installing.state === 'installed') notify();
  };

  if (registration.waiting) {
    notify();
    return () => {
      active = false;
    };
  }

  registration.addEventListener('updatefound', onUpdateFound);

  return () => {
    active = false;
    registration.removeEventListener('updatefound', onUpdateFound);
    releaseInstalling();
  };
}

/**
 * Accepts the update: the waiting worker is told to skip waiting, and the page reloads
 * once the new worker takes control. Calling it twice posts once and reloads once.
 */
export function applyUpdate(
  registration: RegistrationLike,
  options: UpdateOptions = {},
): void {
  const waiting = registration.waiting;
  if (!waiting || applying.has(registration)) return;

  const serviceWorker = options.serviceWorker ?? browserServiceWorker();
  if (!serviceWorker) return;

  applying.add(registration);

  let reloaded = false;
  const onControllerChange = (): void => {
    if (reloaded) return;
    reloaded = true;
    serviceWorker.removeEventListener('controllerchange', onControllerChange);
    (options.reload ?? reloadPage)();
  };

  // Listen before posting: the controller can change as soon as the worker activates.
  serviceWorker.addEventListener('controllerchange', onControllerChange);
  waiting.postMessage({ type: 'SKIP_WAITING' });
}
