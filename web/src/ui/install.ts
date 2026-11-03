/**
 * Installing K7 as an app (A9, A10).
 *
 * Everything here is a function of what the browser has told us, with the window passed in
 * rather than reached for: the situations that matter - a captured prompt, an already
 * standalone window, a browser that can never install - can then be arranged in `node`,
 * and importing this module on the Device copy touches no global.
 *
 * What it keeps is what the browser said and the spec cannot re-derive: the captured
 * `beforeinstallprompt` event, because Chrome allows a single `prompt()` on it and a second
 * call throws, and an `appinstalled` from a tab that is still a tab. Both live per `env`,
 * which is one browser window, for as long as that window is alive.
 */

export type InstallState = 'installable' | 'installed' | 'unsupported' | 'unknown';

export type InstallResult = 'accepted' | 'dismissed' | 'unavailable';

/**
 * `BeforeInstallPromptEvent`, which TypeScript's DOM library does not declare. `userChoice`
 * is the only part worth trusting: Chrome resolves it exactly once, after the dialog.
 */
export interface InstallPromptEvent {
  readonly userChoice: Promise<{ readonly outcome: 'accepted' | 'dismissed' }>;
  prompt(): Promise<void>;
  preventDefault?(): void;
}

export interface MediaQueryLike {
  readonly matches: boolean;
  addEventListener(type: 'change', listener: () => void): void;
  removeEventListener(type: 'change', listener: () => void): void;
}

export interface InstallNavigator {
  readonly userAgent?: string;
  /** Safari's home-screen flag. An iOS standalone window matches no media query at all. */
  readonly standalone?: boolean;
}

/**
 * The slice of `window` the install rules read. A real `Window` satisfies it structurally,
 * so the shell passes `window` without a cast.
 */
export interface InstallEnv {
  readonly location?: { readonly protocol: string };
  readonly navigator?: InstallNavigator;
  readonly matchMedia?: (query: string) => MediaQueryLike;
  addEventListener?: (type: string, listener: (event: unknown) => void) => void;
  removeEventListener?: (type: string, listener: (event: unknown) => void) => void;
}

const STANDALONE_QUERY = '(display-mode: standalone)';

const captured = new WeakMap<InstallEnv, InstallPromptEvent>();

/**
 * `appinstalled` says the app was installed, not that this page became the installed
 * window: the tab that offered the install may keep running as a tab. So the fact is
 * remembered instead of re-derived from `display-mode`, or the control would come back.
 */
const announced = new WeakSet<InstallEnv>();

/** Chrome, Edge and Opera; not Safari, not Firefox, and not any iOS browser. */
const CHROMIUM = /Chrom(?:e|ium)\//;

function isInstalled(env: InstallEnv): boolean {
  if (announced.has(env)) return true;
  if (env.navigator?.standalone === true) return true;
  return env.matchMedia?.(STANDALONE_QUERY)?.matches === true;
}

function isChromium(env: InstallEnv): boolean {
  // A coarse sniff, and the only one available: installability has no feature test. iOS
  // browsers report `CriOS/`/`FxiOS/` and never fire the event, so they land on unsupported,
  // which is what A10 asks for - their answer is Share -> Add to Home Screen.
  return CHROMIUM.test(env.navigator?.userAgent ?? '');
}

/**
 * The insecure origin, which is the Device copy: Chrome never fires `beforeinstallprompt`
 * on `http:`, so waiting for it would leave a control that can never work (A10).
 */
export function isDeviceCopy(env: InstallEnv): boolean {
  return env.location?.protocol === 'http:';
}

/**
 * The order is forced rather than chosen: an installed window is installed even if Chrome
 * is still holding an event, and a browser that cannot install is a different fact from a
 * Chromium that has not said anything yet.
 */
export function readInstallState(env: InstallEnv): InstallState {
  if (isInstalled(env)) return 'installed';
  if (captured.has(env)) return 'installable';
  if (isDeviceCopy(env)) return 'unsupported';
  if (!isChromium(env)) return 'unsupported';
  return 'unknown';
}

/**
 * Subscribes to the three signals Chrome gives an install, and reports the state each one
 * leaves behind. Only changes are reported: the shell re-renders on every call, and a
 * second `beforeinstallprompt` on an already installable page changed nothing.
 */
export function watchInstall(
  env: InstallEnv,
  onChange: (state: InstallState) => void,
): () => void {
  let last = readInstallState(env);

  const notify = (): void => {
    const next = readInstallState(env);
    if (next === last) return;
    last = next;
    onChange(next);
  };

  const onPrompt = (event: unknown): void => {
    const promptEvent = promptEventOf(event);
    if (promptEvent !== null) {
      // Without this Chrome shows its own mini-infobar, and A10 wants one explicit control.
      promptEvent.preventDefault?.();
      captured.set(env, promptEvent);
    }
    notify();
  };

  const onInstalled = (): void => {
    // The event is worth nothing once the app is installed, and holding it invites a prompt
    // that Chrome will refuse.
    captured.delete(env);
    announced.add(env);
    notify();
  };

  const media = env.matchMedia?.(STANDALONE_QUERY);
  media?.addEventListener('change', notify);
  env.addEventListener?.('beforeinstallprompt', onPrompt);
  env.addEventListener?.('appinstalled', onInstalled);

  return () => {
    media?.removeEventListener('change', notify);
    env.removeEventListener?.('beforeinstallprompt', onPrompt);
    env.removeEventListener?.('appinstalled', onInstalled);
  };
}

/**
 * Replays the captured event. The event is spent whether the user accepts or dismisses -
 * Chrome throws `InvalidStateError` on the second `prompt()` - so the answer is also the
 * last thing this window can say about installing.
 */
export async function promptInstall(env: InstallEnv): Promise<InstallResult> {
  const event = captured.get(env);
  if (event === undefined) return 'unavailable';
  captured.delete(env);

  try {
    await event.prompt();
    const choice = await event.userChoice;
    return choice.outcome === 'accepted' ? 'accepted' : 'dismissed';
  } catch {
    // Chrome refused to show the dialog: an install control that resolves to nothing is
    // more honest than an unhandled rejection behind a button.
    return 'unavailable';
  }
}

/**
 * The event arrives as `unknown`, because listeners see whatever the browser dispatched and
 * the DOM library has no type for this one. Only the two members the flow uses are checked.
 */
function promptEventOf(event: unknown): InstallPromptEvent | null {
  if (typeof event !== 'object' || event === null) return null;
  const { prompt } = event as { prompt?: unknown };
  if (typeof prompt !== 'function') return null;
  const { userChoice } = event as { userChoice?: unknown };
  if (!(userChoice instanceof Promise)) return null;
  return event as InstallPromptEvent;
}
