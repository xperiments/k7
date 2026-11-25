import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';

import { k7artAnswer } from './k7art-dev';

/** A build shaped like the editor's: a document, hashed assets, the fonts, the guide. */
function build(root: string): void {
  mkdirSync(join(root, 'assets'), { recursive: true });
  mkdirSync(join(root, 'fonts'), { recursive: true });
  writeFileSync(join(root, 'index.html'), '<!doctype html><title>K7Art</title>');
  writeFileSync(join(root, 'assets/index-abc.js'), 'export const editor = 1;');
  writeFileSync(join(root, 'fonts/archivo-narrow-600.ttf'), Buffer.from([0, 1, 2]));
  writeFileSync(join(root, 'skill.md'), '# the guide');
}

describe('/k7art on the development origin', () => {
  let dist = '';
  let empty = '';

  beforeAll(() => {
    dist = mkdtempSync(join(tmpdir(), 'k7art-dist-'));
    build(dist);
    empty = mkdtempSync(join(tmpdir(), 'k7art-empty-'));
  });

  afterAll(() => {
    rmSync(dist, { recursive: true, force: true });
    rmSync(empty, { recursive: true, force: true });
  });

  it('is not the Site and not the App', () => {
    for (const url of ['/', '/index.html', '/app', '/assets/index-abc.js', '/k7artx']) {
      expect(k7artAnswer(dist, url)).toBeNull();
    }
  });

  it('answers the address with the editor, not with the Site', () => {
    for (const url of ['/k7art', '/k7art/']) {
      const answer = k7artAnswer(dist, url);
      expect(answer?.status).toBe(200);
      expect(answer?.type).toBe('text/html; charset=utf-8');
      expect(String(answer?.body)).toContain('<title>K7Art</title>');
    }
  });

  it('answers the guide and the build under the base the page was built for', () => {
    const guide = k7artAnswer(dist, '/k7art/skill.md');
    expect(guide?.status).toBe(200);
    expect(guide?.type).toBe('text/markdown; charset=utf-8');
    expect(String(guide?.body)).toBe('# the guide');

    const asset = k7artAnswer(dist, '/k7art/assets/index-abc.js?t=1');
    expect(asset?.status).toBe(200);
    expect(asset?.type).toBe('text/javascript; charset=utf-8');
    expect(String(asset?.body)).toContain('editor');

    const font = k7artAnswer(dist, '/k7art/fonts/archivo-narrow-600.ttf');
    expect(font?.status).toBe(200);
    expect(font?.type).toBe('font/ttf');
    expect(Buffer.isBuffer(font?.body)).toBe(true);
  });

  it('says so when the build is missing instead of serving the Site', () => {
    const answer = k7artAnswer(empty, '/k7art');
    expect(answer?.status).toBe(503);
    expect(answer?.type).toBe('text/plain; charset=utf-8');
    expect(String(answer?.body)).toContain('npm run build:editor');
    expect(String(answer?.body)).not.toContain('<title>');
  });

  it('refuses a file the build does not carry, and one outside it', () => {
    const absent = k7artAnswer(dist, '/k7art/assets/not-there.js');
    expect(absent?.status).toBe(404);
    expect(absent?.type).toBe('text/plain; charset=utf-8');

    const outside = k7artAnswer(dist, '/k7art/../../package.json');
    expect(outside?.status).toBe(404);
    expect(String(outside?.body)).toContain('outside the build');
  });
});
