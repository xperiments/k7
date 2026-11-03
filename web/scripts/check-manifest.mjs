#!/usr/bin/env node
// A9 check: the App must install from Chrome, and this manifest is the only thing Chrome
// reads to decide. Run after `npm run build`; every problem names the field that would make
// the install silently unavailable, so the CI failure says what to fix.
//
//   node scripts/check-manifest.mjs [dist-dir]   (default: web/dist)
//
// Node 22, ESM, no dependencies. Exit 1 if any A9 field is missing or wrong.
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const dist = process.argv[2] ? resolve(process.argv[2]) : resolve(here, '..', 'dist');
const manifestPath = join(dist, 'manifest.webmanifest');
// Readable in CI logs (relative when it is under the working directory), unambiguous when not.
const shown = (path) => {
  const relativePath = relative(process.cwd(), path);
  return relativePath && !relativePath.startsWith('..') ? relativePath : path;
};
const label = shown(manifestPath);

const ORIGIN = 'https://k7.xperiments.dev/';
const problems = [];
const fail = (message) => problems.push(message);

let manifest;
try {
  manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));
} catch (error) {
  const why = error.code === 'ENOENT' ? 'does not exist' : `is not readable JSON (${error.message})`;
  console.error(`check-manifest: ${label} ${why} — run \`npm run build\` first`);
  process.exit(1);
}

if (typeof manifest !== 'object' || manifest === null || Array.isArray(manifest)) {
  console.error(`check-manifest: ${label} is not a JSON object — run \`npm run build\` again`);
  process.exit(1);
}

const text = (value) => (typeof value === 'string' ? value.trim() : '');
const pathname = (value) => {
  try {
    return new URL(value, ORIGIN).pathname;
  } catch {
    return null;
  }
};

// A9/A12: the id is the app's identity to Chrome. "/" at the new origin; changing it
// silently leaves the old install behind.
if (!text(manifest.id)) {
  fail('id is missing (A9: "/" — Chrome uses it as the app identity)');
} else if (manifest.id !== '/') {
  fail(`id is "${manifest.id}", A9/A12 require "/" (another id is another app)`);
}

// A9: both names are "K7". A9 and this check also refuse the pre-rename name.
for (const field of ['name', 'short_name']) {
  const value = text(manifest[field]);
  if (!value) {
    fail(`${field} is missing (A9: "K7")`);
  } else if (value.toLowerCase() === 'xtape') {
    fail(`${field} is still "xtape" — the App is K7 (A9)`);
  }
}

// A9: start_url "/" within scope "/".
const scopePath = text(manifest.scope) ? pathname(manifest.scope) : null;
if (!text(manifest.scope)) {
  fail('scope is missing (A9: "/")');
} else if (scopePath !== '/') {
  fail(`scope is "${manifest.scope}", A9 requires "/"`);
}

if (!text(manifest.start_url)) {
  fail('start_url is missing (A9: "/")');
} else {
  const startPath = pathname(manifest.start_url);
  if (startPath !== '/') {
    fail(`start_url is "${manifest.start_url}", A9 requires "/" within scope "/"`);
  } else if (scopePath !== null && !startPath.startsWith(scopePath)) {
    fail(`start_url "${manifest.start_url}" is outside scope "${manifest.scope}" (A9)`);
  }
}

// A9: "browser" is a bookmark, not an installed app.
if (!text(manifest.display)) {
  fail('display is missing (A9: "standalone")');
} else if (text(manifest.display).toLowerCase() === 'browser') {
  fail('display is "browser" — that is a bookmark, not an installable app (A9: "standalone")');
}

for (const field of ['theme_color', 'background_color']) {
  if (!text(manifest[field])) fail(`${field} is missing (A9)`);
}

// A9: a 192, a 512, and a 512 whose purpose includes "maskable", or Android crops it.
const icons = Array.isArray(manifest.icons) ? manifest.icons.filter((icon) => icon && typeof icon === 'object') : [];
if (!Array.isArray(manifest.icons) || manifest.icons.length === 0) {
  fail('icons is missing or empty (A9: 192, 512 and a 512 maskable icon)');
}

const sizesOf = (icon) => (text(icon.sizes) ? text(icon.sizes).toLowerCase().split(/\s+/) : []);
const purposesOf = (icon) =>
  (text(icon.purpose) ? text(icon.purpose).toLowerCase().split(/\s+/) : ['any']).filter(Boolean);

const has192 = icons.some((icon) => sizesOf(icon).includes('192x192'));
const has512 = icons.some((icon) => sizesOf(icon).includes('512x512'));
const hasMaskable512 = icons.some(
  (icon) => sizesOf(icon).includes('512x512') && purposesOf(icon).includes('maskable'),
);

if (!has192) fail('no 192x192 icon in icons (A9)');
if (!has512) fail('no 512x512 icon in icons (A9)');
if (!hasMaskable512) {
  fail('no 512x512 icon with purpose "maskable" (A9: the Android launcher crops the cassette without one)');
}

// An entry that names a file the build did not emit is an install with a broken icon, so
// check the ones this origin serves: root-relative or relative src, never another origin.
icons.forEach((icon, index) => {
  const src = text(icon.src);
  if (/^[a-z][a-z0-9+.-]*:/i.test(src) || src.startsWith('//')) return;
  if (!existsSync(join(dist, src.replace(/^\.?\//, '')))) {
    fail(`icons[${index}].src is "${src}" but that file is not in ${shown(dist)}`);
  }
});

if (problems.length > 0) {
  console.error(`check-manifest: ${problems.length} problem${problems.length === 1 ? '' : 's'} in ${label}`);
  for (const problem of problems) console.error(`  - ${problem}`);
  process.exit(1);
}

console.log(
  `check-manifest: ok — ${label} is installable (id "/", start_url "/" in scope "/", ` +
    'display "standalone", 192, 512 and 512 maskable icons)',
);
