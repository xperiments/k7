#!/usr/bin/env node
// D14's second guard, and L2/L3's stale host, checked in the artifact itself: a page that
// ships with the development origin would let a deployed App talk to a development
// Launcher, and `http://xtape.local` is a host the renamed device no longer answers on.
// Either one in `dist` is a mistake with a security consequence, not a typo.
//
//   node scripts/check-artifact.mjs [dir]   (default: web/dist)
//
// Text files only: a dev origin inside a PNG or a .wasm is not a URL any browser follows.
// Node 22, ESM, no dependencies. Exit 1 listing every offending file and line.
import { closeSync, openSync, readFileSync, readSync, readdirSync } from 'node:fs';
import { dirname, extname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const dist = process.argv[2] ? resolve(process.argv[2]) : resolve(here, '..', 'dist');
// Readable in CI logs (relative when it is under the working directory), unambiguous when not.
const shown = (path) => {
  const relativePath = relative(process.cwd(), path);
  return relativePath && !relativePath.startsWith('..') ? relativePath : path;
};
const label = shown(dist);

const FORBIDDEN = [
  {
    needle: 'k7-dev.xperiments.dev',
    why: 'the development origin belongs to a development Shell only, never to a shipped App (k7-deploy D14, k7-app L4)',
  },
  {
    needle: 'http://xtape.local',
    why: 'the device is k7.local (k7-app L2/L3); the old host in a shipping page is a stale trust decision',
  },
];

// Binary formats, skipped without reading: an origin in their bytes is not a live URL.
const BINARY_EXTENSIONS = new Set([
  '.png', '.jpg', '.jpeg', '.gif', '.webp', '.avif', '.ico', '.bmp', '.tif', '.tiff',
  '.woff', '.woff2', '.ttf', '.otf', '.eot',
  '.mp3', '.flac', '.wav', '.ogg', '.opus', '.m4a', '.aac', '.mp4', '.webm', '.mov', '.mkv',
  '.wasm', '.onnx', '.bin', '.gz', '.br', '.zip', '.tar', '.pdf', '.pt', '.safetensors',
]);

function* walk(dir) {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) yield* walk(path);
    else if (entry.isFile()) yield path;
    // Symlinks are skipped: a loop here would hang CI, and dist has none.
  }
}

// A text file is one whose extension is not a known binary format and whose first block
// has no NUL. That covers extensionless files without trusting a list of extensions.
function isText(path) {
  if (BINARY_EXTENSIONS.has(extname(path).toLowerCase())) return false;
  const fd = openSync(path, 'r');
  try {
    const block = Buffer.alloc(8192);
    const read = readSync(fd, block, 0, block.length, 0);
    return !block.subarray(0, read).includes(0);
  } finally {
    closeSync(fd);
  }
}

function snippet(line, index, length) {
  const start = Math.max(0, index - 60);
  const end = Math.min(line.length, index + length + 60);
  const text = line.slice(start, end).replace(/\s+/g, ' ').trim();
  return `${start > 0 ? '…' : ''}${text}${end < line.length ? '…' : ''}`;
}

let files;
try {
  files = [...walk(dist)];
} catch (error) {
  console.error(`check-artifact: cannot walk ${label} (${error.code ?? error.message}) — run \`npm run build\` first`);
  process.exit(1);
}

let scanned = 0;
const findings = [];

for (const file of files) {
  let text;
  try {
    if (!isText(file)) continue;
    scanned += 1;
    text = readFileSync(file, 'utf8');
  } catch (error) {
    console.error(`check-artifact: cannot read ${shown(file)} (${error.code ?? error.message})`);
    process.exit(1);
  }

  for (const [lineNumber, line] of text.split('\n').entries()) {
    for (const rule of FORBIDDEN) {
      let index = line.indexOf(rule.needle);
      while (index !== -1) {
        findings.push({
          file: shown(file),
          line: lineNumber + 1,
          column: index + 1,
          rule,
          snippet: snippet(line, index, rule.needle.length),
        });
        index = line.indexOf(rule.needle, index + rule.needle.length);
      }
    }
  }
}

if (findings.length > 0) {
  for (const finding of findings) {
    console.error(
      `check-artifact: ${finding.file}:${finding.line}:${finding.column} ` +
        `contains "${finding.rule.needle}"`,
    );
    console.error(`    ${finding.rule.why}`);
    console.error(`    ${finding.snippet}`);
  }
  console.error(
    `check-artifact: ${findings.length} forbidden string${findings.length === 1 ? '' : 's'} ` +
      `in ${label} — a shipping artifact carries neither a development origin nor the pre-rename host`,
  );
  process.exit(1);
}

console.log(`check-artifact: ok — ${scanned} text files in ${label}, no development origin and no http://xtape.local`);
