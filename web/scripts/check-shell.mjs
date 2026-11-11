#!/usr/bin/env node
// The worker's shell is the App, not the Site (k7-site.md S5, D3): `shell.json` is the list
// the worker precaches, and an `/index.html` in it would put the Site — the page that names a
// firmware version — behind a cache-first path that can outlive a release. The build writes
// the list; this checks the artifact CI is about to ship.
//
//   node scripts/check-shell.mjs [dir]   (default: web/dist)
//
// Node 22, ESM, no dependencies. Exit 1 listing every problem.
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const dist = process.argv[2] ? resolve(process.argv[2]) : resolve(here, '..', 'dist');
// Readable in CI logs (relative when it is under the working directory), unambiguous when not.
const shown = (path) => {
  const relativePath = relative(process.cwd(), path);
  return relativePath && !relativePath.startsWith('..') ? relativePath : path;
};
const label = shown(dist);
const problems = [];

for (const document of ['index.html', 'app.html']) {
  if (!existsSync(join(dist, document))) {
    problems.push(`${document} is missing — the build emits the Site and the App as two documents`);
  }
}

let shell;
try {
  shell = JSON.parse(readFileSync(join(dist, 'shell.json'), 'utf8'));
} catch (error) {
  console.error(
    `check-shell: ${label}/shell.json ${error.code === 'ENOENT' ? 'does not exist' : 'is not readable JSON'} — run \`npm run build\` first`,
  );
  process.exit(1);
}

const assets = Array.isArray(shell?.assets) ? shell.assets : [];
if (assets.length === 0) problems.push('shell.json carries no assets');

if (!assets.includes('/app.html')) {
  problems.push('shell.json does not name /app.html — the App is the shell the worker precaches');
}
for (const site of ['/index.html', '/']) {
  if (assets.includes(site)) {
    problems.push(
      `shell.json names ${site} — that is the Site, and it must always come from the network`,
    );
  }
}

if (problems.length > 0) {
  console.error(`check-shell: ${problems.length} problem${problems.length === 1 ? '' : 's'} in ${label}`);
  for (const problem of problems) console.error(`  - ${problem}`);
  process.exit(1);
}

console.log(
  `check-shell: ok — ${label}/shell.json precaches /app.html (${assets.length} assets) and not the Site`,
);
