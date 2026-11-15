/**
 * A **Playlist** as the file it is on the card.
 *
 * One list is one file, `k7.playlists/<id>.k7list`, and the format is
 * `docs/specs/playlist-format.md`: a version line, the id, the name, the creation time, the
 * row count, then one Snapshot path per line. The device reads and writes exactly this
 * (`components/portable/playlist_file`), and so does this browser, so the two agree byte for
 * byte and a list can be written by either side (ADR 0094, ADR 0095).
 *
 * The module is pure: no handles, no storage, no React. `cardLists.ts` is what puts a file on
 * a card; this is what decides whether the bytes are a list at all.
 */

/** The directory at a card's root that holds one file per list. */
export const PLAYLIST_DIR = 'k7.playlists';
export const PLAYLIST_EXT = '.k7list';

/** The format's caps, the same ones the device enforces. */
export const MAX_ROWS = 8192;
export const NAME_MAX_BYTES = 63;
export const PATH_MAX_BYTES = 1024;

/** One list, with its rows in play order. */
export interface ListFile {
  /** 16 lowercase hex characters; the id `select` and `k7.select` carry. */
  id: string;
  name: string;
  /** Unix seconds. */
  createdAt: number;
  /** Snapshot paths: below the card's `music/`, `/`-separated, no leading slash. */
  rows: string[];
}

/** A file that is not a list this build reads. The message says which line and why. */
export class ListFileError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ListFileError';
  }
}

/** The file's name for a list: its id, so renaming a list never moves the file. */
export function listFileName(id: string): string {
  return `${id}${PLAYLIST_EXT}`;
}

/** The path below the card's root. */
export function listPath(id: string): string {
  return `${PLAYLIST_DIR}/${listFileName(id)}`;
}

/** Whether a directory entry is a list file, and its id when it is. */
export function listIdOf(fileName: string): string | null {
  if (!fileName.toLowerCase().endsWith(PLAYLIST_EXT)) return null;
  const id = fileName.slice(0, -PLAYLIST_EXT.length).toLowerCase();
  return /^[0-9a-f]{16}$/.test(id) && id !== '0000000000000000' ? id : null;
}

/* FNV-1a 64, the hash the id and a Track's path already use. */
const FNV_OFFSET = 0xcbf29ce484222325n;
const FNV_PRIME = 0x100000001b3n;
const MASK_64 = 0xffffffffffffffffn;

function fnv(h: bigint, bytes: Uint8Array): bigint {
  let hash = h;
  for (const byte of bytes) {
    hash ^= BigInt(byte);
    hash = (hash * FNV_PRIME) & MASK_64;
  }
  return hash;
}

function fnvLine(h: bigint, text: string): bigint {
  const withNewline = fnv(h, new TextEncoder().encode(text));
  return fnv(withNewline, new Uint8Array([0x0a]));
}

/**
 * The list's content fingerprint: FNV-1a 64 over the name, the creation time and every row,
 * each followed by a newline. Content, not bytes, so a file re-saved with CRLF hashes the same.
 * Written as 16 lowercase hex characters.
 *
 * This is the value `GET /api/playlists` reports and the page compares with its own, which is
 * what makes "edited, the player has the older copy" a fact (spec §7).
 */
export function listFingerprint(list: Pick<ListFile, 'name' | 'createdAt' | 'rows'>): string {
  let hash = fnvLine(FNV_OFFSET, list.name);
  hash = fnvLine(hash, String(list.createdAt));
  for (const row of list.rows) hash = fnvLine(hash, row);
  return hash.toString(16).padStart(16, '0');
}

/**
 * Refuse a list the format does not allow, before it reaches a card. The same rules the reader
 * applies, so this browser can never write a file the device would refuse.
 */
export function validateList(list: ListFile): void {
  if (!/^[0-9a-f]{16}$/.test(list.id) || list.id === '0000000000000000') {
    throw new ListFileError('an id is 16 lowercase hex characters and is not zero');
  }
  const nameIssue = nameProblem(list.name);
  if (nameIssue) throw new ListFileError(nameIssue);
  if (!Number.isInteger(list.createdAt) || list.createdAt < 0) {
    throw new ListFileError('the creation time is not a whole number of seconds');
  }
  if (list.rows.length > MAX_ROWS) throw new ListFileError(`a list holds at most ${MAX_ROWS} rows`);
  for (const row of list.rows) {
    const issue = pathProblem(row);
    if (issue) throw new ListFileError(issue);
  }
}

/** A list's file text, in the one canonical form both sides write. */
export function encodeList(list: ListFile): string {
  validateList(list);
  const lines = [
    'K7LIST 1',
    `id ${list.id}`,
    `name ${list.name}`,
    `created ${list.createdAt}`,
    `rows ${list.rows.length}`,
    ...list.rows,
  ];
  return `${lines.join('\n')}\n`;
}

function controlBytes(text: string): boolean {
  for (const character of text) {
    const code = character.codePointAt(0) ?? 0;
    if (code < 0x20 || code === 0x7f) return true;
  }
  return false;
}

/** UTF-8 length, which is what the format's caps are in. */
function byteLength(text: string): number {
  return new TextEncoder().encode(text).length;
}

function nameProblem(name: string): string | null {
  if (name.length === 0) return 'the name is empty';
  if (byteLength(name) > NAME_MAX_BYTES) return `the name is over ${NAME_MAX_BYTES} bytes`;
  if (name.startsWith(' ') || name.endsWith(' ')) return 'the name begins or ends with a space';
  if (controlBytes(name)) return 'the name carries a control character';
  return null;
}

/** A row as the format allows it: a relative path below the card's music directory. */
export function pathProblem(path: string): string | null {
  if (path.length === 0) return 'a row is empty';
  if (byteLength(path) > PATH_MAX_BYTES) return `a row is over ${PATH_MAX_BYTES} bytes`;
  if (path.startsWith('/')) return 'a row is an absolute path';
  if (path.includes('..')) return 'a row climbs out of the music directory';
  if (controlBytes(path)) return 'a row carries a control character';
  return null;
}

/**
 * Read a list's file, or say why it is not one. `fileName` is the file the text came from:
 * the id inside must agree with it, which is the rule that keeps a renamed or copied file from
 * claiming an identity it does not have. Line endings are accepted as CRLF and the last line's
 * newline may be missing; everything else is the format's own refusal.
 */
export function readList(text: string, fileName: string): ListFile {
  const lines = text.split(/\r?\n/);
  if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop();

  const fail = (at: number, why: string): never => {
    throw new ListFileError(`${fileName}, line ${at + 1}: ${why}`);
  };

  if (lines.length < 5) fail(0, 'a list has a header of five lines');
  const [versionLine, idLine, nameLine, createdLine, rowsLine] = lines;

  const field = (line: string, key: string, at: number): string => {
    if (!line.startsWith(`${key} `)) fail(at, `expected “${key}”`);
    return line.slice(key.length + 1);
  };

  if (versionLine !== 'K7LIST 1') {
    fail(0, versionLine.startsWith('K7LIST ') ? 'an unknown format version' : 'expected K7LIST 1');
  }
  const id = field(idLine, 'id', 1);
  const name = field(nameLine, 'name', 2);
  const created = field(createdLine, 'created', 3);
  const declared = field(rowsLine, 'rows', 4);

  if (!/^[0-9a-f]{16}$/.test(id) || id === '0000000000000000') {
    fail(1, 'an id is 16 lowercase hex characters and is not zero');
  }
  if (id !== listIdOf(fileName)) fail(1, `the id inside does not match the file name`);
  const nameIssue = nameProblem(name);
  if (nameIssue) fail(2, nameIssue);
  if (!/^\d+$/.test(created)) fail(3, 'the creation time is not a number');
  if (!/^\d+$/.test(declared) || Number(declared) > MAX_ROWS) {
    fail(4, `the row count is not a number up to ${MAX_ROWS}`);
  }

  const count = Number(declared);
  const rows = lines.slice(5);
  if (rows.length !== count) {
    fail(4, `the file says ${count} row${count === 1 ? '' : 's'} and carries ${rows.length}`);
  }
  rows.forEach((row, at) => {
    const issue = pathProblem(row);
    if (issue) fail(5 + at, issue);
  });

  return { id, name, createdAt: Number(created), rows };
}
