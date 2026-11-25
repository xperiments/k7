import { readFileSync, statSync } from 'node:fs';
import { extname, resolve, sep } from 'node:path';

/**
 * `/k7art` on the development origin: the editor's own build, at the base it ships under
 * (S1 of `docs/specs/k7-site.md`).
 *
 * The editor is a second Vite project (`tools/LabelEditor/`, ADR 0112). Its build carries
 * `base: "/k7art/"`, which is the only way its page and its `lvgl.js`, fonts and skill
 * resolve; its own development server runs at `:5178` on `base: "/"`, where the editor is
 * edited. The Site's development server knows neither, so before this module `/k7art` fell
 * through to the Site's document — the editor's address answered with the wrong page, which
 * is a shape the origin never ships.
 *
 * So the development origin answers `/k7art` from the editor's build: the bytes that ship,
 * under the base they were built for. A request outside `/k7art` is not this module's and
 * returns `null`; a request the build cannot answer says which build is missing instead of
 * falling back to the Site.
 */

/** One answer to one `/k7art` request, or `null` when the request is not one. */
export type K7artAnswer = {
  status: 200 | 404 | 503;
  type: string;
  body: Buffer | string;
};

/** The document, and the weight and guide the editor's page loads. */
const TYPES: Record<string, string> = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.map': 'application/json; charset=utf-8',
  '.md': 'text/markdown; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.ttf': 'font/ttf',
  '.wasm': 'application/wasm',
};

const NOT_BUILT =
  'the K7Art Editor is not built, so the development origin has no /k7art to serve. ' +
  'Run `npm run build:editor` at the repository root (or `npm run dev` in ' +
  'tools/LabelEditor for the editor on its own at :5178).';

function read(file: string): Buffer | null {
  try {
    return statSync(file).isFile() ? readFileSync(file) : null;
  } catch {
    return null;
  }
}

function missing(status: 404 | 503, body: string): K7artAnswer {
  return { status, type: 'text/plain; charset=utf-8', body };
}

/**
 * The answer for `url` from the editor's build in `dist`, or `null` when the URL is not
 * under `/k7art`. `/k7art` and `/k7art/` are the editor's document, as a directory index;
 * everything else is a file of the build, and a request that climbs out of `dist` is
 * refused rather than read.
 */
export function k7artAnswer(dist: string, url: string): K7artAnswer | null {
  const path = (url.split('?')[0] ?? '').split('#')[0] ?? '';
  if (path !== '/k7art' && path !== '/k7art/' && !path.startsWith('/k7art/')) return null;

  const root = resolve(dist);
  let rel: string;
  try {
    rel =
      path === '/k7art' || path === '/k7art/'
        ? 'index.html'
        : decodeURIComponent(path.slice('/k7art/'.length));
  } catch {
    return missing(404, '/k7art: the path is not decodable.\n');
  }

  const file = resolve(root, rel);
  if (!file.startsWith(root + sep)) return missing(404, `/k7art/${rel} is outside the build.\n`);

  /* No build at all: every `/k7art` request says the same thing, so the address explains
   * itself even before the first asset is asked for. */
  if (!read(resolve(root, 'index.html'))) return missing(503, `${NOT_BUILT}\n`);

  const body = read(file);
  if (!body) {
    return missing(
      404,
      `/k7art/${rel} is not in the editor's build. ` +
        'Run `npm run build:editor` at the repository root to rebuild it.\n',
    );
  }

  return { status: 200, type: TYPES[extname(file).toLowerCase()] ?? 'application/octet-stream', body };
}
