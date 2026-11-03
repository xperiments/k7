import { describe, expect, it } from 'vitest';
import pkg from '../package.json';
import { cacheKey } from '../src/config';
import { ANALYZER_VERSION } from '../src/domain/version';

/**
 * What makes one stored Analysis the same as another.
 *
 * The key is the content hash, the analysis pipeline's version and the model set. The app's
 * own version is deliberately absent: `k7-analysis` exists so a card measured once is not
 * measured again, and taking the version from a release threw every entry away on every
 * deploy — including deploys that changed nothing about measuring a Track (ADR 0066).
 */
describe('the analysis cache key', () => {
  it('is the content hash, the pipeline version and the model set', () => {
    const hash = 'ab'.repeat(32);
    expect(cacheKey(hash, 'modelset')).toBe(`${hash}:${ANALYZER_VERSION}:modelset`);
  });

  it('moves when the bytes or the models do, and not when the app does', () => {
    const base = cacheKey('h1', 'm1');
    expect(cacheKey('h2', 'm1')).not.toBe(base);
    expect(cacheKey('h1', 'm2')).not.toBe(base);
    // The pipeline version is a literal, not `__APP_VERSION__`. If this ever fails, a
    // release has started invalidating every card again.
    expect(ANALYZER_VERSION).not.toBe(pkg.version);
    expect(cacheKey('h1', 'm1')).not.toContain(`:${pkg.version}:`);
  });
});
