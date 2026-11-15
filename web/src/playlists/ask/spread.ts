/**
 * The tracks one answer is drawn from.
 *
 * `library_search` answers in library order, a page at a time. For an ask that names no group
 * or artist that means the first page of a broad filter — "10 rock tracks" on a card whose
 * rock starts with one artist is ten tracks from that artist's first album, not ten rock
 * tracks. The search is right and the answer is wrong, because the App asked for one page and
 * showed it.
 *
 * So an unpaged ask that names nothing is drawn from the matched window instead: the App reads
 * up to two pages (the player answers at most 200 rows for one search), and keeps one track per
 * artist, spread across the window in library order, until the count is met; if the window has
 * fewer artists than that, the rest is filled at even steps through the tracks left. A sentence
 * that names a group or artist, and a stated offset or sort, keep their page: a name is a
 * request for that name, and a page is a page.
 *
 * The window is the player's page ceiling, not the App's choice: a library with more matches
 * than 200 is spread over its first 200, and a player-side sample is the follow-up if that
 * bites. The image source has no such ceiling and is read the same way.
 */

import type { McpSearch, McpTrack, SearchArgs } from '../../device/mcp';

/** One page of the player's search: its `limit` ceiling. */
export const PAGE = 100;

/** The most rows two pages can read: the player refuses an offset + limit beyond 200. */
export const WINDOW = 200;

/** The source's page when the Intent names no count; both sources default to 50 rows. */
export const DEFAULT_COUNT = 50;

/**
 * Whether this answer is drawn from across the library. A name is a request for that name; an
 * offset or a sort is a request for a page. Everything else — a genre, a mood, a range — is a
 * filter, and its answer should sample the library.
 */
export function spreadApplies(intent: SearchArgs): boolean {
  return !intent.query && intent.offset === undefined && intent.sort === undefined;
}

/** The artist a track is credited to: its tag when it has one, else the path's first folder. */
export function artistOf(track: McpTrack): string {
  const tagged = track.analysis?.artist?.trim();
  if (tagged) return tagged.toLowerCase();
  return (track.path.split('/')[0] ?? track.path).toLowerCase();
}

/** `count` evenly spaced indices over `[0, total)`, both ends included, never a repeat. */
function spreadIndices(total: number, count: number): number[] {
  if (count <= 0 || total <= 0) return [];
  if (count === 1) return [0];
  const last = total - 1;
  return Array.from({ length: count }, (_, i) => Math.round((i * last) / (count - 1)));
}

/**
 * `count` tracks out of `tracks`, one per artist while artists remain and spread across them
 * in library order; then the rest at even steps through the tracks left. The result is in
 * `tracks`' own order, so an answer is still A–Z.
 */
export function spreadTracks(tracks: readonly McpTrack[], count: number): McpTrack[] {
  if (count <= 0) return [];
  if (tracks.length <= count) return [...tracks];

  const groups = new Map<string, McpTrack[]>();
  for (const track of tracks) {
    const artist = artistOf(track);
    const group = groups.get(artist);
    if (group) group.push(track);
    else groups.set(artist, [track]);
  }

  const artists = [...groups.values()];
  const picked: McpTrack[] = [];
  if (artists.length >= count) {
    for (const index of spreadIndices(artists.length, count)) picked.push(artists[index][0]);
  } else {
    for (const group of artists) picked.push(group[0]);
    const chosen = new Set(picked);
    const rest = tracks.filter((track) => !chosen.has(track));
    for (const index of spreadIndices(rest.length, count - picked.length)) {
      picked.push(rest[index]);
    }
  }

  const place = new Map(tracks.map((track, index) => [track, index]));
  return picked.sort((a, b) => (place.get(a) ?? 0) - (place.get(b) ?? 0));
}

/**
 * The search behind one answer. An unpaged ask that names nothing reads the matched window and
 * draws the answer from it; every other Intent is one search, exactly as asked.
 */
export async function spreadAnswer(
  search: (args: SearchArgs) => Promise<McpSearch>,
  intent: SearchArgs,
): Promise<McpSearch> {
  if (!spreadApplies(intent)) return search(intent);

  const count = intent.limit ?? DEFAULT_COUNT;
  const first = await search({ ...intent, limit: PAGE, offset: 0 });
  const rows = [...first.tracks];
  if (first.matched > rows.length) {
    const next = await search({ ...intent, limit: PAGE, offset: PAGE });
    rows.push(...next.tracks);
  }

  const tracks = spreadTracks(rows, count);
  return {
    query: first.query,
    matched: first.matched,
    truncated: first.matched > tracks.length,
    tracks,
  };
}
