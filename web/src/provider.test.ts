import { afterEach, describe, expect, it, vi } from 'vitest';
import {
  completionsUrl,
  pageOrigin,
  providerBase,
  providerFailureSentence,
  PROVIDER_PRESETS,
} from './provider';

/**
 * No network here: `fetch` is a stub, and the point of these tests is the two things the
 * module exists for — one URL out of whatever was pasted, and a failure that says whether
 * the origin or the endpoint is the problem.
 */

afterEach(() => {
  vi.unstubAllGlobals();
});

function stubFetch(impl: (url: string, init: RequestInit | undefined) => Promise<unknown>) {
  const spy = vi.fn(impl);
  vi.stubGlobal('fetch', spy);
  return spy;
}

function stubOrigin(origin = 'http://xtape.local') {
  vi.stubGlobal('location', { origin });
}

describe('providerBase', () => {
  it('trims, and drops slashes and a pasted completions path', () => {
    expect(providerBase('https://api.openai.com/v1')).toBe('https://api.openai.com/v1');
    expect(providerBase('  http://localhost:11434/v1/  ')).toBe('http://localhost:11434/v1');
    expect(providerBase('http://localhost:11434/v1/chat/completions')).toBe(
      'http://localhost:11434/v1',
    );
    expect(providerBase('http://localhost:11434/v1/chat/completions/')).toBe(
      'http://localhost:11434/v1',
    );
    // A path below the host is the user's, not something to normalise away.
    expect(providerBase('http://192.168.1.4:8787/openai/v1')).toBe(
      'http://192.168.1.4:8787/openai/v1',
    );
  });
});

describe('completionsUrl', () => {
  it('posts to one endpoint whatever the base URL looks like', () => {
    expect(completionsUrl('https://api.openai.com/v1/')).toBe(
      'https://api.openai.com/v1/chat/completions',
    );
    expect(completionsUrl('http://localhost:11434/v1/chat/completions')).toBe(
      'http://localhost:11434/v1/chat/completions',
    );
    expect(completionsUrl('   ')).toBe('');
  });
});

describe('pageOrigin', () => {
  it('is the page it is served from, and empty outside a browser', () => {
    expect(pageOrigin()).toBe('');
    stubOrigin('http://192.168.1.42');
    expect(pageOrigin()).toBe('http://192.168.1.42');
  });
});

describe('providerFailureSentence', () => {
  it('names the origin the Provider has to allow when it answers without CORS', async () => {
    stubOrigin();
    const spy = stubFetch(async () => ({ type: 'opaque' }));

    const sentence = await providerFailureSentence('http://localhost:11434/v1/');

    // The probe is the one request CORS cannot stop, and it is asked of the base URL.
    expect(spy).toHaveBeenCalledTimes(1);
    const [url, init] = spy.mock.calls[0];
    expect(url).toBe('http://localhost:11434/v1/models');
    expect(init).toMatchObject({ method: 'GET', mode: 'no-cors' });

    expect(sentence).toContain('http://xtape.local');
    expect(sentence).toContain('OLLAMA_ORIGINS=http://xtape.local');
    expect(sentence).not.toContain('Nothing answered');
  });

  it('offers the local knob for loopback by name or address', async () => {
    for (const local of ['http://127.0.0.1:11434/v1', 'http://[::1]:11434/v1']) {
      stubOrigin();
      stubFetch(async () => ({ type: 'opaque' }));
      const sentence = await providerFailureSentence(local);
      expect(sentence).toContain('OLLAMA_ORIGINS=http://xtape.local');
      vi.unstubAllGlobals();
    }
  });

  it('does not offer OLLAMA_ORIGINS to a hosted Provider', async () => {
    stubOrigin();
    stubFetch(async () => ({ type: 'opaque' }));

    const sentence = await providerFailureSentence('https://api.example.com/v1');

    expect(sentence).not.toContain('OLLAMA_ORIGINS');
    expect(sentence).toContain('http://xtape.local');
    expect(sentence).toMatch(/hosted Provider/);
  });

  it('says nothing answered when the endpoint really is down', async () => {
    stubOrigin();
    stubFetch(async () => {
      throw new TypeError('Failed to fetch');
    });

    const sentence = await providerFailureSentence('http://localhost:11434/v1');

    expect(sentence).toContain('Nothing answered at http://localhost:11434/v1');
    expect(sentence).not.toContain('OLLAMA_ORIGINS');
  });

  it('does not probe an endpoint that has no base URL', async () => {
    const spy = stubFetch(async () => ({ type: 'opaque' }));

    const sentence = await providerFailureSentence('   ');

    expect(spy).not.toHaveBeenCalled();
    expect(sentence).toMatch(/base URL/);
  });

  it('never prints an empty origin as if it were one', async () => {
    stubFetch(async () => ({ type: 'opaque' }));

    const sentence = await providerFailureSentence('http://localhost:11434/v1');

    expect(sentence).not.toContain('undefined');
    expect(sentence).not.toContain('OLLAMA_ORIGINS');
  });
});

describe('PROVIDER_PRESETS', () => {
  it('fills a base URL the page can append to exactly once', () => {
    for (const preset of PROVIDER_PRESETS) {
      expect(providerBase(preset.baseUrl)).toBe(preset.baseUrl);
      expect(completionsUrl(preset.baseUrl)).toBe(`${preset.baseUrl}/chat/completions`);
    }
  });

  it('names each preset once, and asks for a model where it leaves one out', () => {
    const labels = PROVIDER_PRESETS.map((preset) => preset.label);
    expect(new Set(labels).size).toBe(labels.length);
    for (const preset of PROVIDER_PRESETS) {
      expect(preset.modelPlaceholder.length).toBeGreaterThan(0);
      expect(preset.note.length).toBeGreaterThan(0);
      // A preset with no model is one whose model is the user's: the field must say what to
      // type instead of claiming a default it cannot know.
      if (!preset.model) expect(preset.modelPlaceholder).not.toBe('');
    }
  });

  it('points the free presets at a free model, and Ollama at this machine', () => {
    const byId = new Map(PROVIDER_PRESETS.map((preset) => [preset.id, preset]));

    expect(byId.get('gemini')?.baseUrl).toBe(
      'https://generativelanguage.googleapis.com/v1beta/openai',
    );
    expect(byId.get('gemini')?.model).toBe('gemini-3.8-flash');
    expect(byId.get('openrouter')?.model.endsWith(':free')).toBe(true);
    expect(new URL(byId.get('ollama')?.baseUrl ?? '').hostname).toBe('localhost');
    // Ollama serves whatever was pulled, so the preset cannot guess a model name.
    expect(byId.get('ollama')?.model).toBe('');
  });
});
