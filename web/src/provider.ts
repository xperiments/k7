/**
 * The **Provider**'s HTTP corner in one place: the URL it is asked at, and the sentence a
 * failure is worth.
 *
 * Settings' Test and Ask's `requestIntent` ask the same endpoint and can fail the same two
 * ways, and only one of them is "could not be reached". A Provider on this machine — Ollama
 * on `http://localhost:11434/v1` — is a different origin from the page the device serves, so
 * it has to name `http://xtape.local` in its CORS allowlist; until it does, the browser
 * refuses the reply before the page sees a byte, and `fetch` rejects with a bare `TypeError`
 * that is indistinguishable from "nothing is listening". The page cannot read the reason out
 * of the error, so it asks the endpoint again in the one way CORS cannot stop (ADR 0056).
 *
 * Keeping this here rather than in each caller is the point: a user who fixes the origin for
 * Test has fixed it for Ask too, and both say the same thing about it.
 */

/** The base URL without trailing slashes or a pasted `/chat/completions`. */
export function providerBase(baseUrl: string): string {
  return baseUrl
    .trim()
    .replace(/\/+$/, '')
    .replace(/\/chat\/completions$/, '')
    .replace(/\/+$/, '');
}

/** The endpoint a chat completion is posted to; `''` when no base URL is configured. */
export function completionsUrl(baseUrl: string): string {
  const base = providerBase(baseUrl);
  return base ? `${base}/chat/completions` : '';
}

/** The origin this page is served from, which a Provider elsewhere has to allow. */
export function pageOrigin(): string {
  return typeof location === 'undefined' ? '' : location.origin;
}

/**
 * A starting point for Settings — the base URL and, where it is a fixed choice, the model.
 *
 * A preset is not a default: nothing is configured until one is clicked, the key is always the
 * user's own, and no endpoint is shipped with a credential (ADR 0053, and the shell is served
 * to anyone on the LAN). The free ones are free tiers, not free of accounts, so the note under
 * the row says where the key comes from and what it costs.
 */
export interface ProviderPreset {
  id: 'ollama' | 'gemini' | 'openrouter';
  /** The button's label, and the name the note uses. */
  label: string;
  baseUrl: string;
  /** Filled into the Model field; `''` when only the user's own models make sense. */
  model: string;
  /** What the Model field asks for when the preset leaves it empty. */
  modelPlaceholder: string;
  /** Shown after the label once the preset is chosen — a fragment, not a sentence. */
  note: string;
}

export const PROVIDER_PRESETS: readonly ProviderPreset[] = [
  {
    id: 'ollama',
    label: 'Ollama',
    // The desktop app serves this on this machine; the page's origin has to be allowed by
    // it, which is the one thing a preset cannot do (see the README, and ADR 0056).
    baseUrl: 'http://localhost:11434/v1',
    model: '',
    modelPlaceholder: 'llama3.2',
    note: 'fits any model you have pulled and costs nothing; its server has to allow this page (OLLAMA_ORIGINS).',
  },
  {
    id: 'gemini',
    label: 'Gemini',
    baseUrl: 'https://generativelanguage.googleapis.com/v1beta/openai',
    model: 'gemini-3.8-flash',
    modelPlaceholder: 'gemini-3.8-flash',
    note: 'its free tier covers gemini-3.8-flash; a key from AI Studio is the only setup.',
  },
  {
    id: 'openrouter',
    label: 'OpenRouter',
    baseUrl: 'https://openrouter.ai/api/v1',
    model: 'google/gemma-4-26b-a4b-it:free',
    modelPlaceholder: 'google/gemma-4-26b-a4b-it:free',
    note: 'free models end in :free and allow 50 requests a day; a key from OpenRouter.',
  },
];

/** Long enough for a loopback server, short enough not to stall Test on a dead host. */
const PROBE_TIMEOUT_MS = 2500;

/**
 * Whether anything answers at the endpoint, asked in `no-cors` mode: the browser sends the
 * request without checking the reply's origin, so an opaque response proves the server is
 * there even when every CORS-mode request to it fails. The status is not knowable this way
 * and does not matter — a `404` from the port is still an answer.
 */
async function answers(baseUrl: string): Promise<boolean> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), PROBE_TIMEOUT_MS);
  try {
    await fetch(`${baseUrl}/models`, {
      method: 'GET',
      mode: 'no-cors',
      cache: 'no-store',
      signal: controller.signal,
    });
    return true;
  } catch {
    return false;
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Whether the endpoint is a server on this machine, which is where `OLLAMA_ORIGINS` is the
 * knob. A hosted Provider is somebody else's CORS policy: it either allows browsers or it
 * does not, and nothing in this page or in Settings can change that.
 */
function isLocal(baseUrl: string): boolean {
  try {
    const host = new URL(baseUrl).hostname;
    return host === 'localhost' || host === '127.0.0.1' || host === '::1' || host === '[::1]';
  } catch {
    return false;
  }
}

/**
 * One sentence for a Provider request that threw, for both Settings' Test and Ask.
 *
 * There are two answers, so the sentence is a question the page can settle: if the endpoint
 * answers when CORS is not consulted, then it is up and its CORS answer is what refused the
 * page; if nothing answers, the base URL or the server is the problem. The origin named is
 * this page's own, because that — not the Provider's address — is the string its allowlist
 * has to contain.
 */
export async function providerFailureSentence(baseUrl: string): Promise<string> {
  const base = providerBase(baseUrl);
  if (!base) return 'The Provider could not be reached. Check its base URL in Settings.';

  if (!(await answers(base))) {
    return `Nothing answered at ${base}. Check the base URL and that the Provider's server is running.`;
  }

  const origin = pageOrigin();
  const allowed = origin
    ? `its CORS answer does not allow this page (${origin})`
    : "its CORS answer does not allow this page's origin";

  let hint = '';
  if (origin && isLocal(base)) {
    hint = ` For Ollama, set OLLAMA_ORIGINS=${origin} and restart it.`;
  } else if (!isLocal(base)) {
    // The page's origin is not this computer's to configure, so name that instead of a knob
    // the endpoint does not have.
    hint =
      ' A hosted Provider has to allow that origin in its own CORS settings; one that refuses browser requests at all cannot be asked from this page.';
  }
  return `The Provider answered, but the browser blocked the reply: ${allowed}.${hint}`;
}
