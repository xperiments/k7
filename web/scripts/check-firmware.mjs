#!/usr/bin/env node
// R4 (k7-site.md §4): the manifests the Site flashes from must match the files it serves.
//
// Reads web/firmware.json and web/firmware-player.json, hashes every part under
// web/public/firmware/, and fails on a missing file, a size or hash that disagrees, a version
// that is not the repository's, or a shape the page cannot read. No dependencies, Node 22.
//
//   node scripts/check-firmware.mjs [repo-root]
import { createHash } from 'node:crypto';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = process.argv[2] ? resolve(process.argv[2]) : resolve(here, '..', '..');
const shown = (path) => {
  const rel = relative(process.cwd(), path);
  return rel && !rel.startsWith('..') ? rel : path;
};

/** Each board's manifest and the directory its release publishes to (k7-site.md F9). */
const BOARDS = [
  { id: 'k7', manifest: 'web/firmware.json', dir: 'web/public/firmware' },
  { id: 'player', manifest: 'web/firmware-player.json', dir: 'web/public/firmware/player' },
];

const problems = [];
const fail = (message) => problems.push(message);

const versionPath = join(root, 'VERSION');
if (!existsSync(versionPath)) fail('VERSION is missing at the repository root');
const version = existsSync(versionPath) ? readFileSync(versionPath, 'utf8').trim() : '';

function checkPart(board, part) {
  const label = `${board.id} ${board.manifest}`;
  if (typeof part?.file !== 'string' || !part.file) {
    fail(`${label}: a part has no file name`);
    return;
  }
  if (!Number.isInteger(part.offset) || part.offset < 0) {
    fail(`${label}: ${part.file} has no offset`);
  }
  if (typeof part.sha256 !== 'string' || !/^[0-9a-f]{64}$/.test(part.sha256)) {
    fail(`${label}: ${part.file} has no sha256`);
  }
  const path = join(root, board.dir, board.version, part.file);
  if (!existsSync(path)) {
    fail(`${label}: ${shown(path)} does not exist`);
    return;
  }
  const data = readFileSync(path);
  if (data.length !== part.size) {
    fail(`${label}: ${part.file} is ${data.length} B, the manifest says ${part.size} B`);
  }
  const digest = createHash('sha256').update(data).digest('hex');
  if (digest !== part.sha256) {
    fail(`${label}: ${part.file} hashes ${digest.slice(0, 16)}…, the manifest says ${part.sha256.slice(0, 16)}…`);
  }
}

for (const board of BOARDS) {
  const path = join(root, board.manifest);
  if (!existsSync(path)) {
    fail(`${board.manifest} does not exist`);
    continue;
  }
  let manifest;
  try {
    manifest = JSON.parse(readFileSync(path, 'utf8'));
  } catch (error) {
    fail(`${board.manifest} is not readable JSON (${error.message})`);
    continue;
  }
  if (typeof manifest.version !== 'string' || !/^\d+\.\d+\.\d+$/.test(manifest.version)) {
    fail(`${board.manifest}: version is not semver`);
    continue;
  }
  if (manifest.version !== version) {
    fail(`${board.manifest}: version ${manifest.version} disagrees with VERSION ${version}`);
  }
  if (typeof manifest.chip !== 'string' || !manifest.chip) fail(`${board.manifest}: no chip`);
  const flash = manifest.flash ?? {};
  for (const field of ['mode', 'size', 'freq']) {
    if (typeof flash[field] !== 'string' || !flash[field]) {
      fail(`${board.manifest}: flash.${field} is missing`);
    }
  }
  if (!Array.isArray(manifest.parts) || manifest.parts.length === 0) {
    fail(`${board.manifest}: no parts`);
    continue;
  }
  for (const part of manifest.parts) {
    checkPart({ ...board, version: manifest.version }, part);
  }
}

if (problems.length > 0) {
  console.error('check-firmware: the manifests and the files disagree:');
  for (const problem of problems) console.error(`  ${problem}`);
  process.exit(1);
}
console.log(
  `check-firmware: ${BOARDS.length} manifests match their parts (${version})`,
);
