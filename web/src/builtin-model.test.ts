import { describe, expect, it } from 'vitest';
import { assistantFailure, assistantSentence, assistantState, stateFrom } from './builtin-model';

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
