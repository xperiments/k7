#!/usr/bin/env node
// R4 (k7-site.md §4): the manifests the Site flashes from must match the files it serves.
//
// Reads web/firmware.json and web/firmware-player.json, hashes every part under
// web/public/firmware/, and fails on a missing file, a size or hash that disagrees, a version
// that is not the repository's, or a shape the page cannot read. No dependencies, Node 22.
//
//   node scripts/check-firmware.mjs [repo-root]
import { createHash, createPublicKey, verify as verifySignature } from 'node:crypto';
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
const parsed = new Map();
const fail = (message) => problems.push(message);

/** The signed image a player takes (player-update.md U1-U3, ADR 0116). */
const PLAYER_HEADER = 128;
const PLAYER_SIGNATURE_MAX = 72;
const PLAYER_IMAGE_OFFSET = 0x710000;
const PLAYER_PARTITION_SIZE = 2 * 1024 * 1024;
const PLAYER_PUBLIC_KEY = join(root, 'player', 'release-key.pub.pem');

const sha256Hex = (data) => createHash('sha256').update(data).digest('hex');

/**
 * R4's second verification: the bytes the K7 hands a player are an image this project's key
 * signed, and no other. The header is checked field by field and the signature with the public
 * key committed beside the player's source, so an image a player would refuse cannot ship.
 */
function checkSignedImage(label, data, version) {
  if (data.length < PLAYER_HEADER) {
    fail(`${label}: shorter than the ${PLAYER_HEADER}-byte header`);
    return;
  }
  if (data.subarray(0, 4).toString('latin1') !== 'K7PU') {
    fail(`${label}: the magic is not K7PU`);
    return;
  }
  if (data[4] !== 1) fail(`${label}: header version ${data[4]}`);
  if (data[5] !== 1) fail(`${label}: board ${data[5]} is not the classic ESP32`);
  const signatureLength = data[6];
  if (signatureLength < 1 || signatureLength > PLAYER_SIGNATURE_MAX) {
    fail(`${label}: signature length ${signatureLength}`);
    return;
  }
  if (data[7] !== 0 || data[18] !== 0 || data[19] !== 0) {
    fail(`${label}: a reserved byte is not zero`);
  }
  if (data.subarray(124, 128).some((byte) => byte !== 0)) {
    fail(`${label}: the reserved tail is not zero`);
  }
  const imageLength = data.readUInt32BE(8);
  if (imageLength + PLAYER_HEADER !== data.length) {
    fail(`${label}: the header says ${imageLength} B of image and the file is ${data.length - PLAYER_HEADER} B`);
    return;
  }
  if (imageLength > PLAYER_PARTITION_SIZE - PLAYER_HEADER) {
    fail(`${label}: ${imageLength} B does not fit the player's partition`);
  }
  const headerVersion = `${data.readUInt16BE(12)}.${data.readUInt16BE(14)}.${data.readUInt16BE(16)}`;
  if (headerVersion !== version) {
    fail(`${label}: the header says version ${headerVersion}, the release is ${version}`);
  }
  const image = data.subarray(PLAYER_HEADER);
  if (sha256Hex(image) !== data.subarray(20, 52).toString('hex')) {
    fail(`${label}: the header's SHA-256 does not match the image`);
  }
  if (!existsSync(PLAYER_PUBLIC_KEY)) {
    fail(`${label}: ${shown(PLAYER_PUBLIC_KEY)} is missing`);
    return;
  }
  let key;
  try {
    key = createPublicKey(readFileSync(PLAYER_PUBLIC_KEY));
  } catch (error) {
    fail(`${label}: the public key cannot be read (${error.message})`);
    return;
  }
  const signature = data.subarray(52, 52 + signatureLength);
  if (!verifySignature('sha256', image, key, signature)) {
    fail(`${label}: the signature does not verify with ${shown(PLAYER_PUBLIC_KEY)}`);
  }
}

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
    if (board.id === 'k7' && part.offset === PLAYER_IMAGE_OFFSET) {
      const path = join(root, board.dir, manifest.version, part.file);
      if (existsSync(path)) {
        checkSignedImage(`k7 ${manifest.version} ${part.file}`, readFileSync(path), manifest.version);
      }
    }
  }
  if (board.id === 'player' && manifest.image) {
    const image = manifest.image;
    if (typeof image.file !== 'string' || !image.file) {
      fail(`${board.manifest}: image has no file name`);
    } else {
      const path = join(root, board.dir, manifest.version, image.file);
      if (!existsSync(path)) {
        fail(`${board.manifest}: ${shown(path)} does not exist`);
      } else {
        const data = readFileSync(path);
        if (data.length !== image.size) {
          fail(`${board.manifest}: ${image.file} is ${data.length} B, the manifest says ${image.size} B`);
        }
        if (sha256Hex(data) !== image.sha256) {
          fail(`${board.manifest}: ${image.file} hashes ${sha256Hex(data).slice(0, 16)}…, the manifest says ${String(image.sha256).slice(0, 16)}…`);
        }
        checkSignedImage(`player ${manifest.version} ${image.file}`, data, manifest.version);
      }
    }
  }
  parsed.set(board.id, manifest);
}

const k7 = parsed.get('k7');
const player = parsed.get('player');
if (player?.image) {
  const carried = k7?.parts?.find((part) => part.offset === PLAYER_IMAGE_OFFSET);
  if (!carried) {
    fail(
      `the K7's manifest has no part at 0x${PLAYER_IMAGE_OFFSET.toString(16)}, ` +
        `but the player's image is published`,
    );
  } else if (carried.sha256 !== player.image.sha256 || carried.size !== player.image.size) {
    fail(`the K7's ${carried.file} is not the image the player's release published`);
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
