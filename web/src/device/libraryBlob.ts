/**
 * The device's full Library, read from `/api/library.bin`.
 *
 * The MCP `library_list` call is capped at 200 entries, so on a real card its answer is a
 * partial listing: the Media albums could not tell "not on this card" from "past the cap"
 * and fell back to the whole browser cache. The binary view is the device's own arena —
 * every node, every track, from the card in the S3 — and is small (254 KB for 3,485 tracks).
 * This parses just enough of it to build the presence the Media area needs.
 *
 * Layout (`components/portable/library/include/library.h`): a 64-byte header, 32-byte nodes,
 * a child-edge array, a track-index array, then the UTF-8 string table. This reader only
 * walks `parent` and the name fields; it validates every offset it uses.
 */
import type { Presence } from '../media/albums';
import { foldPath } from '../media/paths';
import { measureableName } from '../domain/analysis';

const HEADER_SIZE = 64;
const NODE_SIZE = 32;
const MAGIC = 0x424c5458; /* "XTLB" little-endian */
const NONE = 0xffffffff;

export function presenceFromLibraryBlob(bytes: Uint8Array): Presence {
  if (bytes.length < HEADER_SIZE) throw new Error('the library blob is shorter than its header');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint32(0, true) !== MAGIC) throw new Error('this is not a library blob');
  if (view.getUint16(4, true) !== 1) throw new Error('unknown library blob version');
  if (view.getUint16(6, true) !== HEADER_SIZE) throw new Error('bad library header size');

  const nodes = view.getUint32(12, true);
  const edges = view.getUint32(16, true);
  const tracks = view.getUint32(20, true);
  const stringBytes = view.getUint32(24, true);
  if (nodes === 0) throw new Error('the library blob has no root');

  const nodesStart = HEADER_SIZE;
  const edgesStart = nodesStart + nodes * NODE_SIZE;
  const tracksStart = edgesStart + edges * 4;
  const stringsStart = tracksStart + tracks * 4;
  if (stringsStart + stringBytes > bytes.length) throw new Error('the library blob is truncated');

  const strings = bytes.subarray(stringsStart, stringsStart + stringBytes);
  const decoder = new TextDecoder();

  const nameOf = (off: number, len: number): string => {
    if (off + len > stringBytes) throw new Error('a library name is outside the string table');
    return decoder.decode(strings.subarray(off, off + len));
  };

  const paths: string[] = new Array(nodes);
  const present = new Set<string>();
  const measurable: { name: string; path: string }[] = [];
  const unmeasurable: { name: string; path: string }[] = [];

  for (let index = 0; index < nodes; index += 1) {
    const at = nodesStart + index * NODE_SIZE;
    const parent = view.getUint32(at + 12, true);
    const nameOff = view.getUint32(at + 8, true);
    const nameLen = view.getUint16(at + 22, true);
    const kind = view.getUint8(at + 24);
    const name = nameOf(nameOff, nameLen);

    if (parent !== NONE && parent >= index) {
      throw new Error('a library node points at a parent that has not been read yet');
    }
    const path = parent === NONE ? '' : paths[parent] ? `${paths[parent]}/${name}` : name;
    paths[index] = path;

    if (kind !== 1) continue;
    present.add(foldPath(path));
    const entry = { name, path };
    if (measureableName(name) === null) unmeasurable.push(entry);
    else measurable.push(entry);
  }

  const byPath = (left: { path: string }, right: { path: string }) =>
    left.path.localeCompare(right.path);

  // The arena is the whole generation, including its no-card form (a root, no tracks): this
  // is the allowlist, so the Media area never shows an album the card in the S3 does not have.
  return {
    paths: present,
    tracks: measurable.sort(byPath),
    unmeasurable: unmeasurable.sort(byPath),
    complete: true,
  };
}
