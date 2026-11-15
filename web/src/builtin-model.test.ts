import { describe, expect, it, vi } from 'vitest';
import { askModel, assistantFailure, assistantSentence, assistantState, stateFrom } from './builtin-model';

/**
 * The built-in model's state is Chrome's answer, not this page's; what is testable here is the
 * mapping and the words. The model itself is not in CI.
 */
describe('stateFrom', () => {
  it('keeps the four answers Chrome gives', () => {
    expect(stateFrom('available')).toBe('available');
    expect(stateFrom('downloadable')).toBe('downloadable');
    expect(stateFrom('downloading')).toBe('downloading');
    expect(stateFrom('unavailable')).toBe('unavailable');
  });

  it('treats an answer it does not know as a machine that cannot run the model', () => {
    expect(stateFrom('something-new')).toBe('unavailable');
  });
});

describe('assistantState', () => {
  it('reports an absent API when Chrome does not expose one', async () => {
    expect(await assistantState()).toBe('absent');
  });
});

describe('assistantSentence', () => {
  it('names the disk floor for a model that can still be fetched', () => {
    expect(assistantSentence('downloadable')).toMatch(/22 GB/);
  });

  it('names both causes of unavailable, because Chrome cannot tell them apart', () => {
    expect(assistantSentence('unavailable')).toMatch(/built-in AI turned on in Chrome/);
  });

  it('says nothing when the model is ready', () => {
    expect(assistantSentence('available')).toBe('');
  });
});

describe('assistantFailure', () => {
  it('names a context overflow as one', () => {
    expect(assistantFailure(new DOMException('too long', 'QuotaExceededError'))).toMatch(/room/);
  });

  it('names a download failure as one', () => {
    expect(assistantFailure(new Error('download failed'))).toMatch(/download/);
  });

  it('falls back to the model not answering', () => {
    expect(assistantFailure(new Error('boom'))).toMatch(/did not answer/);
  });
});

describe('askModel', () => {
  it('leaves the schema out of the input but still constrains the answer', async () => {
    const prompt = vi.fn().mockResolvedValue('{"limit":10}');
    const destroy = vi.fn();
    const create = vi.fn().mockResolvedValue({ prompt, destroy });
    vi.stubGlobal('LanguageModel', { availability: vi.fn(), create });
    try {
      const answer = await askModel({
        system: 'system',
        schema: { type: 'object' },
        sentence: 'ten songs',
        examples: [['play', '{}']],
      });
      expect(answer).toBe('{"limit":10}');
      expect(create).toHaveBeenCalledWith({
        initialPrompts: [
          { role: 'system', content: 'system' },
          { role: 'user', content: 'play' },
          { role: 'assistant', content: '{}' },
        ],
      });
      expect(prompt).toHaveBeenCalledWith(
        'ten songs',
        expect.objectContaining({
          responseConstraint: { type: 'object' },
          omitResponseConstraintInput: true,
        }),
      );
      expect(destroy).toHaveBeenCalledOnce();
    } finally {
      vi.unstubAllGlobals();
    }
  });
});
