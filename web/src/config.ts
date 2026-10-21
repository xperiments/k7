import { ANALYZER_VERSION } from './domain/version';

/** Set to the public repository URL before deploying the analyser. */
export const SOURCE_URL = '';

export function cacheKey(hash: string, modelSet: string): string {
  return `${hash}:${ANALYZER_VERSION}:${modelSet}`;
}
