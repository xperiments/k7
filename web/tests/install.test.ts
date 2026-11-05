import { describe, expect, it } from 'vitest';
import {
  isInsecureOrigin,
  promptInstall,
  readInstallState,
  watchInstall,
  type InstallEnv,
  type InstallPromptEvent,
  type InstallState,
} from '../src/ui/install';

/**
 * A window-like object, because the install rules are about what the browser has told
 * us so far (A9, A10), and the only way to arrange that in node is to fake the browser.
 */

const CHROME_UA =
  'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) ' +
  'Chrome/131.0.0.0 Safari/537.36';
const SAFARI_UA =
  'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) ' +
  'Version/18.1 Safari/605.1.15';
const FIREFOX_UA = 'Mozilla/5.0 (X11; Linux x86_64; rv:133.0) Gecko/20100101 Firefox/133.0';

type Listener = (event: unknown) => void;

class FakeMediaQueryList {
  matches: boolean;
  private readonly listeners = new Set<() => void>();

  constructor(matches: boolean) {
    this.matches = matches;
  }

  get listenerCount(): number {
    return this.listeners.size;
  }

  addEventListener(_type: string, listener: () => void): void {
    this.listeners.add(listener);
  }

  removeEventListener(_type: string, listener: () => void): void {
    this.listeners.delete(listener);
  }

  /** The user puts the app in its own window, or takes it back out. */
  flip(matches: boolean): void {
    this.matches = matches;
    for (const listener of [...this.listeners]) listener();
  }
}

class FakePromptEvent implements InstallPromptEvent {
  prevented = 0;
  prompts = 0;
  readonly userChoice: Promise<{ outcome: 'accepted' | 'dismissed' }>;

  constructor(outcome: 'accepted' | 'dismissed') {
    this.userChoice = Promise.resolve({ outcome });
  }

  preventDefault(): void {
    this.prevented += 1;
  }

  prompt(): Promise<void> {
    this.prompts += 1;
    return Promise.resolve();
  }
}

/** A prompt Chrome refuses to show, because the event was already spent. */
class SpentPromptEvent extends FakePromptEvent {
  override prompt(): Promise<void> {
    return Promise.reject(new Error('InvalidStateError'));
  }
}

interface FakeEnvOptions {
  protocol?: string;
  userAgent?: string;
  standalone?: boolean;
  displayModeStandalone?: boolean;
}

interface FakeWindow {
  env: InstallEnv;
  media: FakeMediaQueryList;
  queries: string[];
  dispatch(type: string, event?: unknown): void;
  listenerCount(type: string): number;
}

function fakeWindow(options: FakeEnvOptions = {}): FakeWindow {
  const listeners = new Map<string, Set<Listener>>();
  const media = new FakeMediaQueryList(options.displayModeStandalone ?? false);
  const queries: string[] = [];

  const env: InstallEnv = {
    location: { protocol: options.protocol ?? 'https:' },
    navigator: {
      userAgent: options.userAgent ?? CHROME_UA,
      ...(options.standalone ? { standalone: true } : {}),
    },
    matchMedia(query) {
      queries.push(query);
      return media;
    },
    addEventListener(type, listener) {
      const forType = listeners.get(type) ?? new Set<Listener>();
      forType.add(listener);
      listeners.set(type, forType);
    },
    removeEventListener(type, listener) {
      listeners.get(type)?.delete(listener);
    },
  };

  return {
    env,
    media,
    queries,
    dispatch(type, event = {}) {
      for (const listener of [...(listeners.get(type) ?? [])]) listener(event);
    },
    listenerCount(type) {
      return listeners.get(type)?.size ?? 0;
    },
  };
}

/** A Chromium window that has just offered the install prompt. */
function offeredWindow(outcome: 'accepted' | 'dismissed') {
  const fake = fakeWindow();
  const stop = watchInstall(fake.env, () => {});
  const event = new FakePromptEvent(outcome);
  fake.dispatch('beforeinstallprompt', event);
  return { ...fake, event, stop };
}

describe('what the browser has said about installing', () => {
  it('is installed once the window already runs standalone', () => {
    const { env } = fakeWindow({ displayModeStandalone: true });
    expect(readInstallState(env)).toBe('installed');
  });

  it('is installed on an iOS home-screen window, which matches no media query', () => {
    // Safari reports `navigator.standalone` instead of `display-mode: standalone`.
    const { env } = fakeWindow({ userAgent: SAFARI_UA, standalone: true });
    expect(readInstallState(env)).toBe('installed');
  });

  it('is unknown on Chromium before it has fired beforeinstallprompt', () => {
    const { env, queries } = fakeWindow();
    expect(readInstallState(env)).toBe('unknown');
    // It has to ask the media query, or an already-installed window is missed.
    expect(queries).toContain('(display-mode: standalone)');
  });

  it('is installable once beforeinstallprompt has been captured', () => {
    const { env, dispatch } = fakeWindow();
    const stop = watchInstall(env, () => {});
    dispatch('beforeinstallprompt', new FakePromptEvent('accepted'));
    expect(readInstallState(env)).toBe('installable');
    stop();
  });

  it('is unsupported on the browsers that never fire it', () => {
    expect(readInstallState(fakeWindow({ userAgent: SAFARI_UA }).env)).toBe('unsupported');
    expect(readInstallState(fakeWindow({ userAgent: FIREFOX_UA }).env)).toBe('unsupported');
  });

  it('is unsupported on an insecure origin, where Chrome cannot install at all', () => {
    // A10: a plain http origin must never be offered an install.
    expect(readInstallState(fakeWindow({ protocol: 'http:' }).env)).toBe('unsupported');
  });

  it('keeps installed ahead of a prompt event Chrome may still be holding', () => {
    const { env, dispatch, media } = fakeWindow();
    const stop = watchInstall(env, () => {});
    dispatch('beforeinstallprompt', new FakePromptEvent('accepted'));
    media.flip(true);
    expect(readInstallState(env)).toBe('installed');
    stop();
  });
});

describe('prompting the install', () => {
  it('is accepted when the user accepts', async () => {
    const { env, event, stop } = offeredWindow('accepted');
    await expect(promptInstall(env)).resolves.toBe('accepted');
    expect(event.prompts).toBe(1);
    stop();
  });

  it('is dismissed when the user dismisses', async () => {
    const { env, event, stop } = offeredWindow('dismissed');
    await expect(promptInstall(env)).resolves.toBe('dismissed');
    expect(event.prompts).toBe(1);
    stop();
  });

  it('is unavailable when no prompt was ever captured', async () => {
    const { env } = fakeWindow();
    await expect(promptInstall(env)).resolves.toBe('unavailable');
  });

  it('spends the event, because Chrome throws if the same one is replayed', async () => {
    const { env, stop } = offeredWindow('dismissed');
    await expect(promptInstall(env)).resolves.toBe('dismissed');
    await expect(promptInstall(env)).resolves.toBe('unavailable');
    expect(readInstallState(env)).toBe('unknown');
    stop();
  });

  it('is unavailable when Chrome refuses to show the prompt at all', async () => {
    const { env, dispatch } = fakeWindow();
    const stop = watchInstall(env, () => {});
    dispatch('beforeinstallprompt', new SpentPromptEvent('accepted'));
    await expect(promptInstall(env)).resolves.toBe('unavailable');
    stop();
  });
});

describe('an insecure origin', () => {
  it('is a plain http origin, where install is impossible', () => {
    expect(isInsecureOrigin(fakeWindow({ protocol: 'http:' }).env)).toBe(true);
  });

  it('is not the app origin', () => {
    expect(isInsecureOrigin(fakeWindow({ protocol: 'https:' }).env)).toBe(false);
  });
});

describe('watching the install signals', () => {
  it('reports installable and suppresses Chrome own banner', () => {
    const { env, dispatch } = fakeWindow();
    const seen: InstallState[] = [];
    const stop = watchInstall(env, (state) => seen.push(state));
    const event = new FakePromptEvent('accepted');

    dispatch('beforeinstallprompt', event);

    expect(seen).toEqual(['installable']);
    // Without preventDefault Chrome shows its own mini-infobar, and A10 wants one
    // explicit control instead.
    expect(event.prevented).toBe(1);
    stop();
  });

  it('reports installed when the app installs', () => {
    const { env, dispatch } = fakeWindow();
    const seen: InstallState[] = [];
    const stop = watchInstall(env, (state) => seen.push(state));

    dispatch('beforeinstallprompt', new FakePromptEvent('accepted'));
    dispatch('appinstalled');

    expect(seen).toEqual(['installable', 'installed']);
    stop();
  });

  it('reports installed when the window enters standalone display-mode', () => {
    const { env, media, queries } = fakeWindow();
    const seen: InstallState[] = [];
    const stop = watchInstall(env, (state) => seen.push(state));

    expect(queries).toContain('(display-mode: standalone)');
    media.flip(true);

    expect(seen).toEqual(['installed']);
    stop();
  });

  it('unsubscribes from every signal it registered', () => {
    const { env, dispatch, media, listenerCount } = fakeWindow();
    const seen: InstallState[] = [];
    const stop = watchInstall(env, (state) => seen.push(state));
    expect(media.listenerCount).toBe(1);

    stop();

    expect(media.listenerCount).toBe(0);
    expect(listenerCount('beforeinstallprompt')).toBe(0);
    expect(listenerCount('appinstalled')).toBe(0);
    dispatch('beforeinstallprompt', new FakePromptEvent('accepted'));
    dispatch('appinstalled');
    media.flip(true);
    expect(seen).toEqual([]);
  });

  it('does not notify twice for a signal that changed nothing', () => {
    const { env, dispatch } = fakeWindow();
    const seen: InstallState[] = [];
    const stop = watchInstall(env, (state) => seen.push(state));

    dispatch('beforeinstallprompt', new FakePromptEvent('accepted'));
    dispatch('beforeinstallprompt', new FakePromptEvent('dismissed'));

    expect(seen).toEqual(['installable']);
    stop();
  });
});
