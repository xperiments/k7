import { describe, expect, it } from 'vitest';
import {
  applyUpdate,
  watchForUpdate,
  type RegistrationLike,
  type ServiceWorkerContainerLike,
  type WorkerLike,
} from '../src/update';

class FakeWorker implements WorkerLike {
  state = 'installing';
  readonly messages: unknown[] = [];
  private readonly listeners = new Set<() => void>();

  postMessage(message: unknown): void {
    this.messages.push(message);
  }

  addEventListener(_type: 'statechange', listener: () => void): void {
    this.listeners.add(listener);
  }

  removeEventListener(_type: 'statechange', listener: () => void): void {
    this.listeners.delete(listener);
  }

  /** The browser's transition to `installed`, including the event. */
  installed(): void {
    this.state = 'installed';
    for (const listener of [...this.listeners]) listener();
  }

  get listenerCount(): number {
    return this.listeners.size;
  }
}

class FakeRegistration implements RegistrationLike {
  waiting: WorkerLike | null = null;
  installing: WorkerLike | null = null;
  private readonly listeners = new Set<() => void>();

  addEventListener(_type: 'updatefound', listener: () => void): void {
    this.listeners.add(listener);
  }

  removeEventListener(_type: 'updatefound', listener: () => void): void {
    this.listeners.delete(listener);
  }

  /** `updatefound`: a new worker has started installing. */
  found(worker: WorkerLike): void {
    this.installing = worker;
    for (const listener of [...this.listeners]) listener();
  }

  get listenerCount(): number {
    return this.listeners.size;
  }
}

class FakeContainer implements ServiceWorkerContainerLike {
  private readonly listeners = new Set<() => void>();

  addEventListener(_type: 'controllerchange', listener: () => void): void {
    this.listeners.add(listener);
  }

  removeEventListener(_type: 'controllerchange', listener: () => void): void {
    this.listeners.delete(listener);
  }

  controllerChanged(): void {
    for (const listener of [...this.listeners]) listener();
  }

  get listenerCount(): number {
    return this.listeners.size;
  }
}

describe('watching for a new version', () => {
  it('is ready immediately when a waiting worker already exists', () => {
    const registration = new FakeRegistration();
    registration.waiting = new FakeWorker();
    const seen: string[] = [];

    const stop = watchForUpdate(registration, () => seen.push('ready'));

    expect(seen).toEqual(['ready']);
    stop();
  });

  it('follows updatefound through the worker installing to waiting', () => {
    const registration = new FakeRegistration();
    const worker = new FakeWorker();
    const seen: string[] = [];

    const stop = watchForUpdate(registration, () => seen.push('ready'));
    expect(seen).toEqual([]);

    registration.found(worker);
    expect(seen).toEqual([]);

    // At `installed` the browser has made it the registration's waiting worker.
    registration.waiting = worker;
    worker.installed();
    expect(seen).toEqual(['ready']);

    // A second statechange cannot produce a second banner.
    worker.installed();
    expect(seen).toEqual(['ready']);
    stop();
  });

  it('does not treat a first install that activates as an update', () => {
    const registration = new FakeRegistration();
    const worker = new FakeWorker();
    const seen: string[] = [];

    const stop = watchForUpdate(registration, () => seen.push('ready'));
    registration.found(worker);
    worker.installed();

    expect(seen).toEqual([]);
    expect(registration.listenerCount).toBe(1);
    stop();
    expect(registration.listenerCount).toBe(0);
  });

  it('stops listening when the caller stops watching', () => {
    const registration = new FakeRegistration();
    const worker = new FakeWorker();
    const seen: string[] = [];

    const stop = watchForUpdate(registration, () => seen.push('ready'));
    stop();

    expect(registration.listenerCount).toBe(0);
    registration.found(worker);
    registration.waiting = worker;
    worker.installed();

    expect(seen).toEqual([]);
    expect(worker.listenerCount).toBe(0);
  });

  it('releases the worker listener once ready', () => {
    const registration = new FakeRegistration();
    const worker = new FakeWorker();
    const seen: string[] = [];

    watchForUpdate(registration, () => seen.push('ready'));
    registration.found(worker);
    expect(worker.listenerCount).toBe(1);

    registration.waiting = worker;
    worker.installed();

    expect(seen).toEqual(['ready']);
    expect(registration.listenerCount).toBe(0);
    expect(worker.listenerCount).toBe(0);
  });
});

describe('applying an update', () => {
  it('asks the waiting worker to activate and reloads once the controller changes', () => {
    const registration = new FakeRegistration();
    const worker = new FakeWorker();
    registration.waiting = worker;
    const container = new FakeContainer();
    let reloads = 0;

    applyUpdate(registration, {
      serviceWorker: container,
      reload: () => {
        reloads += 1;
      },
    });

    expect(worker.messages).toEqual([{ type: 'SKIP_WAITING' }]);
    expect(reloads).toBe(0);

    container.controllerChanged();
    container.controllerChanged();

    expect(reloads).toBe(1);
    expect(container.listenerCount).toBe(0);
  });

  it('does nothing twice, so a double click is one update', () => {
    const registration = new FakeRegistration();
    const worker = new FakeWorker();
    registration.waiting = worker;
    const container = new FakeContainer();
    let reloads = 0;
    const options = {
      serviceWorker: container,
      reload: () => {
        reloads += 1;
      },
    };

    applyUpdate(registration, options);
    applyUpdate(registration, options);

    expect(worker.messages).toEqual([{ type: 'SKIP_WAITING' }]);
    expect(container.listenerCount).toBe(1);

    container.controllerChanged();
    expect(reloads).toBe(1);
  });

  it('does not listen, post or reload when nothing is waiting', () => {
    const registration = new FakeRegistration();
    const container = new FakeContainer();
    let reloads = 0;

    applyUpdate(registration, {
      serviceWorker: container,
      reload: () => {
        reloads += 1;
      },
    });

    expect(container.listenerCount).toBe(0);
    expect(reloads).toBe(0);
  });
});
