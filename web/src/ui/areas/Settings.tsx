import { useId, useState } from 'react';

import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Spinner } from '@/components/ui/spinner';
import { isAbort, searchVideos } from '../../media/youtube';
import {
  completionsUrl,
  providerFailureSentence,
  PROVIDER_PRESETS,
  type ProviderPreset,
} from '../../provider';
import { getSettings, hasProvider, setSettings, type Settings } from '../../settings';
import { plural } from '../format';

/**
 * What this browser remembers for itself.
 *
 * Two settings, neither of which the device ever sees: the **Provider** that the Ask page
 * asks for filters (ADR 0053 -- the model writes filters, never results, and no track is
 * ever described to it), and the Invidious **Instance** the video search talks to.
 *
 * The key is write-only from the page's point of view: it is saved in `localStorage`, it is
 * never rendered back, and the only thing the form says about it afterwards is that it is
 * set. Clearing site data loses all of it, which is the honest cost of not putting a key in
 * the device's flash.
 */

interface Notice {
  kind: 'ok' | 'error';
  text: string;
}

/** The answer a Test produces: one sentence either way. */
interface TestResult {
  ok: boolean;
  text: string;
}

/** The Invidious lookup is one GET; eight seconds is already generous for it. */
const INSTANCE_TEST_TIMEOUT_MS = 8000;

/**
 * A Provider may think before it answers: Gemini 3 and later cannot switch reasoning off, and
 * that latency is the model working, not the network stalling. Twenty seconds is long enough
 * for a reasoning model and still short enough that a wrong URL fails while the user waits.
 */
const PROVIDER_TEST_TIMEOUT_MS = 20_000;

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** The assistant message of an OpenAI-compatible reply, as a string, when there is one. */
function messageContent(payload: unknown): string | null {
  if (!isRecord(payload)) return null;
  const choices = payload.choices;
  if (!Array.isArray(choices) || choices.length === 0) return null;
  const first = choices[0];
  if (!isRecord(first)) return null;
  const content = isRecord(first.message) ? first.message.content : null;
  if (typeof content === 'string') return content;
  if (Array.isArray(content)) {
    const parts = content
      .map((part) => (isRecord(part) && typeof part.text === 'string' ? part.text : ''))
      .join('');
    return parts || null;
  }
  return null;
}

/** The first complete top-level JSON object in a string, or null. */
function firstJsonObject(text: string): unknown | null {
  const start = text.indexOf('{');
  if (start === -1) return null;
  let depth = 0;
  let inString = false;
  let escaped = false;
  for (let index = start; index < text.length; index += 1) {
    const character = text[index];
    if (inString) {
      if (escaped) escaped = false;
      else if (character === '\\') escaped = true;
      else if (character === '"') inString = false;
      continue;
    }
    if (character === '"') inString = true;
    else if (character === '{') depth += 1;
    else if (character === '}') {
      depth -= 1;
      if (depth === 0) {
        try {
          return JSON.parse(text.slice(start, index + 1)) as unknown;
        } catch {
          return null;
        }
      }
    }
  }
  return null;
}

function providerError(payload: unknown): string {
  if (!isRecord(payload)) return '';
  const error = payload.error;
  if (typeof error === 'string') return error;
  if (isRecord(error) && typeof error.message === 'string') return error.message;
  return '';
}

/**
 * One tiny request, one tiny answer: the point is to prove the endpoint, the model and the
 * key work together, not to ask the model anything a user would care about.
 *
 * A throw is not automatically "unreachable": a Provider on this machine is a different
 * origin from the page, so the browser can refuse a reply the server did send. That case is
 * named rather than guessed at — `providerFailureSentence` asks the endpoint whether it is up.
 */
async function testProvider(settings: Settings, signal: AbortSignal): Promise<TestResult> {
  if (!hasProvider(settings)) {
    return { ok: false, text: 'Save a base URL, a model and a key first.' };
  }

  let response: Response;
  try {
    response = await fetch(completionsUrl(settings.providerBaseUrl), {
      method: 'POST',
      headers: {
        'content-type': 'application/json',
        accept: 'application/json',
        authorization: `Bearer ${settings.providerKey}`,
      },
      body: JSON.stringify({
        model: settings.providerModel,
        temperature: 0,
        // No max_tokens: a reasoning model counts its thinking against the cap, and a tight
        // one (this used to be 24) ends the answer empty with finish_reason "length" — the
        // model answered, and the page saw nothing. The prompt keeps the answer short.
        messages: [
          { role: 'system', content: 'You answer with one JSON object and nothing else.' },
          { role: 'user', content: 'Reply with exactly {"ok":true}' },
        ],
      }),
      signal,
    });
  } catch {
    if (signal.aborted) {
      return { ok: false, text: 'The Provider did not answer within twenty seconds.' };
    }
    return { ok: false, text: await providerFailureSentence(settings.providerBaseUrl) };
  }

  const body = await response.text();
  let parsed: unknown = null;
  try {
    parsed = body ? (JSON.parse(body) as unknown) : null;
  } catch {
    /* the status and the answer check below say enough */
  }

  if (!response.ok) {
    const detail = providerError(parsed);
    return {
      ok: false,
      text: `The Provider refused it (${response.status})${detail ? `: ${detail}` : '.'}`,
    };
  }

  const content = messageContent(parsed);
  if (!content) return { ok: false, text: 'The Provider answered without a message.' };

  const json = firstJsonObject(content);
  if (json === null) {
    return {
      ok: false,
      text: `The Provider answered, but not with JSON: “${content.slice(0, 80)}”.`,
    };
  }
  return { ok: true, text: 'The Provider answered with JSON — Ask can ask it for filters.' };
}

function Field({
  label,
  htmlFor,
  hint,
  children,
}: {
  label: string;
  htmlFor: string;
  hint?: string;
  children: React.ReactNode;
}) {
  return (
    <div className="flex flex-col gap-1">
      <label htmlFor={htmlFor} className="text-xs text-muted-foreground">
        {label}
      </label>
      {children}
      {hint ? <p className="text-xs text-muted-foreground">{hint}</p> : null}
    </div>
  );
}

function NoticeLine({ notice }: { notice: Notice }) {
  return (
    <div role="status" className="flex items-start gap-2 text-sm">
      <Badge variant={notice.kind === 'ok' ? 'success-light' : 'destructive-light'}>
        {notice.kind === 'ok' ? 'ok' : 'failed'}
      </Badge>
      <span className="min-w-0 break-words">{notice.text}</span>
    </div>
  );
}

export function SettingsArea() {
  const [saved, setSaved] = useState<Settings>(() => getSettings());

  const [baseUrl, setBaseUrl] = useState(saved.providerBaseUrl);
  const [model, setModel] = useState(saved.providerModel);
  const [key, setKey] = useState('');
  const [instance, setInstance] = useState(saved.invidious);

  const [providerNotice, setProviderNotice] = useState<Notice | null>(null);
  const [instanceNotice, setInstanceNotice] = useState<Notice | null>(null);
  const [testingProvider, setTestingProvider] = useState(false);
  const [testingInstance, setTestingInstance] = useState(false);
  const [preset, setPreset] = useState<ProviderPreset | null>(null);

  const baseUrlId = useId();
  const modelId = useId();
  const keyId = useId();
  const instanceId = useId();

  const keySet = Boolean(saved.providerKey);

  /**
   * A preset fills what the Provider decides — where it lives and, when there is one good
   * answer, which model — and leaves the key and the choice of model where the user's are.
   */
  const applyPreset = (chosen: ProviderPreset) => {
    setBaseUrl(chosen.baseUrl);
    setModel(chosen.model);
    setPreset(chosen);
    setProviderNotice(null);
  };

  const saveProvider = () => {
    const url = baseUrl.trim();
    if (url) {
      try {
        const parsed = new URL(url);
        if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') throw new Error('proto');
      } catch {
        setProviderNotice({
          kind: 'error',
          text: 'The base URL must be an http or https address, for example https://api.openai.com/v1.',
        });
        return;
      }
    }
    const nextKey = key.trim() ? key.trim() : saved.providerKey;
    setSettings({
      providerBaseUrl: url,
      providerModel: model.trim(),
      providerKey: nextKey,
    });
    setSaved(getSettings());
    setKey('');
    setProviderNotice({
      kind: 'ok',
      text: hasProvider()
        ? 'Saved in this browser. The device never receives it.'
        : 'Saved — fill in the base URL, the model and a key to switch the assistant on.',
    });
  };

  const clearKey = () => {
    setSettings({ providerKey: '' });
    setSaved(getSettings());
    setKey('');
    setProviderNotice({ kind: 'ok', text: 'The key is gone from this browser.' });
  };

  const runProviderTest = async () => {
    setTestingProvider(true);
    setProviderNotice(null);
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), PROVIDER_TEST_TIMEOUT_MS);
    try {
      // Test what the form says, not only what was saved: a key is typed and tested before
      // it is stored, and the saved key stands in when the field is left empty.
      const draft: Settings = {
        providerBaseUrl: baseUrl.trim(),
        providerModel: model.trim(),
        providerKey: key.trim() || saved.providerKey,
        invidious: instance.trim(),
      };
      const result = await testProvider(draft, controller.signal);
      setProviderNotice({ kind: result.ok ? 'ok' : 'error', text: result.text });
    } finally {
      clearTimeout(timer);
      setTestingProvider(false);
    }
  };

  const saveInstance = () => {
    const value = instance.trim();
    setSettings({ invidious: value });
    setSaved(getSettings());
    setInstanceNotice({
      kind: 'ok',
      text: value ? 'Saved in this browser.' : 'Cleared — the video search has no instance to ask.',
    });
  };

  const runInstanceTest = async () => {
    setTestingInstance(true);
    setInstanceNotice(null);
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), INSTANCE_TEST_TIMEOUT_MS);
    try {
      const found = await searchVideos('test', instance, controller.signal);
      setInstanceNotice({
        kind: 'ok',
        text: `The instance answered with ${plural(found.length, 'video')}.`,
      });
    } catch (error) {
      if (isAbort(error)) {
        setInstanceNotice({ kind: 'error', text: 'The instance did not answer within eight seconds.' });
      } else {
        setInstanceNotice({
          kind: 'error',
          text: error instanceof Error ? error.message : String(error),
        });
      }
    } finally {
      clearTimeout(timer);
      setTestingInstance(false);
    }
  };

  return (
    <div className="flex flex-col gap-4 p-4 sm:gap-6 sm:p-6">
      <header className="flex flex-col gap-0.5">
        <h1 className="text-xl font-semibold tracking-tight">Settings</h1>
        <p className="text-sm text-muted-foreground">
          What this browser remembers for itself. None of it is sent to the device.
        </p>
      </header>

      <Card>
        <CardHeader>
          <CardTitle>Provider</CardTitle>
          <p className="text-sm text-muted-foreground">
            The OpenAI-compatible endpoint the Ask page asks for filters. The model turns your
            sentence into filters; it never sees your tracks (ADR 0053). No track title, artist,
            path or measurement is ever sent to it.
          </p>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          <div className="flex flex-col gap-1">
            <div className="flex flex-wrap items-center gap-2">
              <span className="text-xs text-muted-foreground">Start from</span>
              {PROVIDER_PRESETS.map((each) => (
                <Button
                  key={each.id}
                  type="button"
                  size="sm"
                  variant="outline"
                  onClick={() => applyPreset(each)}
                >
                  {each.label}
                </Button>
              ))}
            </div>
            {preset ? (
              <p className="text-xs text-muted-foreground">
                <span className="text-foreground">{preset.label}:</span> {preset.note}
              </p>
            ) : null}
          </div>

          <div className="grid grid-cols-1 gap-3 sm:grid-cols-2">
            <Field
              label="Base URL"
              htmlFor={baseUrlId}
              hint="For example https://api.openai.com/v1"
            >
              <Input
                id={baseUrlId}
                type="url"
                value={baseUrl}
                placeholder="https://api.openai.com/v1"
                autoComplete="off"
                spellCheck={false}
                onChange={(event) => setBaseUrl(event.target.value)}
              />
            </Field>
            <Field label="Model" htmlFor={modelId} hint="The model that answers with an Intent">
              <Input
                id={modelId}
                value={model}
                placeholder={preset?.modelPlaceholder ?? 'gpt-4o-mini'}
                autoComplete="off"
                spellCheck={false}
                onChange={(event) => setModel(event.target.value)}
              />
            </Field>
          </div>

          <Field
            label="Key"
            htmlFor={keyId}
            hint={
              keySet
                ? 'A key is saved and is never shown again. Type here to replace it.'
                : 'Saved in this browser’s local storage, never in the device. Ollama asks for one and ignores it, so any word will do.'
            }
          >
            <Input
              id={keyId}
              type="password"
              value={key}
              placeholder={keySet ? '••••••••  (saved)' : 'sk-…'}
              autoComplete="off"
              spellCheck={false}
              onChange={(event) => setKey(event.target.value)}
            />
          </Field>

          {keySet ? (
            <div className="flex flex-wrap items-center gap-2">
              <Badge variant="success-light">
                key set
              </Badge>
              <Button variant="ghost" size="sm" onClick={clearKey}>
                Clear the key
              </Button>
            </div>
          ) : null}

          <div className="flex flex-wrap items-center gap-2">
            <Button size="sm" onClick={saveProvider}>
              Save
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={() => void runProviderTest()}
              disabled={testingProvider}
            >
              Test
            </Button>
            {testingProvider ? <Spinner aria-hidden="true" className="size-3.5" /> : null}
          </div>

          {providerNotice ? <NoticeLine notice={providerNotice} /> : null}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Invidious instance</CardTitle>
          <p className="text-sm text-muted-foreground">
            The server the video search asks. Public instances come and go, so this is a setting
            rather than a constant.
          </p>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          <Field label="Instance" htmlFor={instanceId} hint="An https address, no trailing slash needed">
            <Input
              id={instanceId}
              type="url"
              value={instance}
              placeholder="https://inv.nadeko.net"
              autoComplete="off"
              spellCheck={false}
              onChange={(event) => setInstance(event.target.value)}
            />
          </Field>

          <div className="flex flex-wrap items-center gap-2">
            <Button size="sm" onClick={saveInstance}>
              Save
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={() => void runInstanceTest()}
              disabled={testingInstance}
            >
              Test
            </Button>
            {testingInstance ? <Spinner aria-hidden="true" className="size-3.5" /> : null}
          </div>

          {instanceNotice ? <NoticeLine notice={instanceNotice} /> : null}
        </CardContent>
      </Card>

      <p className="text-xs text-muted-foreground">
        These live in this browser&apos;s local storage and are never written to the device. Clearing
        site data loses them.
      </p>
    </div>
  );
}
