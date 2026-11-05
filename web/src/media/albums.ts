/**
 * The album and artist index: the shape the Media area browses.
 *
 * **Names come from this browser**, not from the device's paging. The cache in
 * `k7-media` holds the title, artist, album, genre and picture of every track this
 * browser read while the card was in the computer; the device's `library_search` pages at
 * `offset <= 199`, so indexing 5,000 tracks by paging would take ~50 requests and still be
 * partial (front door spec §4.3, corrected by the build note). What the device supplies is
 * **existence** -- the track count and generation in `/api/status`, plus one listing walked
 * for track paths -- and, when an album is opened, the measurements for its rows.
 *
 * The rules that follow from that:
 *
 * 1. An album is the title its cache rows share. The artists on those rows are names it
 *    carries, not part of what makes it one album: a compilation is one shelf, not one per
 *    name -- the same rule Process groups its queue by. No cache row, no album, which is why
 *    the empty state says "measure the card once".
 * 2. Existence decides what counts. When the device listed *the whole* library, a cache row
 *    whose track is no longer on the card is left out: the album does not claim a file that
 *    is gone. When the listing was partial (it caps at 200 entries), the cache is shown
 *    whole, because absence from a partial listing means nothing.
 * 3. The tracks the device listed and this browser has no cache row for are **not
 *    measured** here. They are reported as such and link to Process, never guessed at.
 */

import { useEffect, useMemo, useState } from 'react';
import { useCardScope } from '../card/session';
import { measureableName } from '../domain/analysis';
import { libraryStats, listLibrary, type LibraryListing } from '../device/mcp';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { listMedia, type CachedCover, type CachedMedia } from '../storage/media';
import { foldPath } from './paths';

export const UNKNOWN_ARTIST = 'Unknown artist';
export const UNKNOWN_ALBUM = 'Unknown album';

/** `library_list` refuses a limit above this, so one call cannot list a large library. */
const PRESENCE_LIMIT = 200;

export interface Album {
  /** The title *and* the identity: two rows sharing it are the same album, and the route carries it. */
  album: string;
  /** The artists its rows name, sorted. More than one is a compilation. */
  artists: string[];
  /** The cache rows, in path order. */
  rows: CachedMedia[];
  tracks: number;
  cover: CachedCover | null;
}

/** A track the device listed, with the name the listing gave it. */
export interface DeviceTrack {
  name: string;
  path: string;
}

export interface Presence {
  /** The measurable track paths the device listed, folded by `foldPath` (a card has no case). */
  paths: ReadonlySet<string>;
  /** Device tracks this App can open: the only ones that can be "not measured". */
  tracks: DeviceTrack[];
  /** Device tracks in a format this App never opens (a `.wav`, say): on the card, not work left. */
  unmeasurable: DeviceTrack[];
  /** True when the listing covered the whole library, so absence means the track is gone. */
  complete: boolean;
}

export interface MediaIndex {
  albums: Album[];
  /** Measurable device tracks with no cache row: measured nowhere this browser knows. */
  unmeasured: DeviceTrack[];
  /** Device tracks this App cannot open, reported apart from the ones it simply has not read. */
  unmeasurable: DeviceTrack[];
  presence: Presence | null;
}

function clean(value: string | undefined): string {
  return typeof value === 'string' ? value.trim() : '';
}

/** An album's title and the artist a row names, with the fallbacks the display uses. */
export function albumOf(row: Pick<CachedMedia, 'artist' | 'album'>): {
  artist: string;
  album: string;
} {
  return {
    artist: clean(row.artist) || UNKNOWN_ARTIST,
    album: clean(row.album) || UNKNOWN_ALBUM,
  };
}

/**
 * A path folded the way the card's filesystem is: case is not information on FAT32, and the
 * device folds it too (`library_find`). Two sides that name one file differently by case are
 * naming one file, so a joined path is compared folded and kept as it arrived.
 */
export { foldPath } from './paths';

function byText(left: string, right: string): number {
  return left.localeCompare(right, undefined, { sensitivity: 'base' });
}

function byPath(left: CachedMedia, right: CachedMedia): number {
  return left.path.localeCompare(right.path);
}

/** Group the cache into albums. Pure, so the rule above is testable by reading. */
export function buildMediaIndex(
  cache: ReadonlyMap<string, CachedMedia>,
  presence: Presence | null,
): MediaIndex {
  const rows = [...cache.values()].filter(
    (row) => !(presence?.complete ?? false) || presence?.paths.has(foldPath(row.path)),
  );

  // Keyed by the title alone: a second artist on the same title adds a name to the album it
  // already is, it does not start another album.
  const groups = new Map<string, { album: Album; artists: Set<string> }>();
  for (const row of rows) {
    const names = albumOf(row);
    const entry = groups.get(names.album);
    if (entry) {
      entry.album.rows.push(row);
      entry.artists.add(names.artist);
      continue;
    }
    groups.set(names.album, {
      album: { album: names.album, artists: [], rows: [row], tracks: 0, cover: null },
      artists: new Set([names.artist]),
    });
  }

  const albums = [...groups.values()].map(({ album, artists }) => ({
    ...album,
    artists: [...artists].sort(byText),
  }));
  for (const album of albums) {
    album.rows.sort(byPath);
    album.tracks = album.rows.length;
    album.cover = album.rows.find((row) => row.cover)?.cover ?? null;
  }
  albums.sort((left, right) => byText(left.album, right.album));

  // Joined folded: the cache key is the browser's spelling, the listing's is the device's, and
  // on a card they can differ by case without being two files.
  const cached = new Set([...cache.keys()].map(foldPath));
  const unmeasured = (presence?.tracks ?? []).filter((track) => !cached.has(foldPath(track.path)));

  return {
    albums,
    unmeasured,
    unmeasurable: presence?.unmeasurable ?? [],
    presence,
  };
}

/**
 * One listing, walked for track paths. `expectedTracks` comes from `/api/status`, so the
 * listing can say whether it covered the library: the device caps a call at 200 entries,
 * and a count that matches the Library's own track count is the only proof of completeness.
 *
 * Three kinds of entry share that listing. A directory is not a track. A track this App can
 * measure is what "unmeasured" is decided on. A track it cannot (the device also plays WAV)
 * is kept apart: the browser will never open it, so reporting it as not measured would say
 * the card is half done while it is not.
 */
export function presenceOf(listing: LibraryListing, expectedTracks: number): Presence {
  const paths = new Set<string>();
  const tracks: DeviceTrack[] = [];
  const unmeasurable: DeviceTrack[] = [];
  let listed = 0;

  for (const entry of listing.entries ?? []) {
    if (entry.type !== 'track') continue;
    const folded = foldPath(entry.path);
    if (paths.has(folded)) continue;
    paths.add(folded);
    listed += 1;
    const track = { name: entry.name, path: entry.path };
    if (measureableName(entry.name) === null) unmeasurable.push(track);
    else tracks.push(track);
  }

  // A listing with no tracks proves nothing: the card may be out of the player, where the
  // library is empty and this browser's cache is still the only record of what was measured.
  const complete = listed > 0 && !listing.truncated && listed === expectedTracks;

  const byPath = (left: DeviceTrack, right: DeviceTrack) => byText(left.path, right.path);
  return { paths, tracks: tracks.sort(byPath), unmeasurable: unmeasurable.sort(byPath), complete };
}

/** The listing, or `null` when the device did not answer -- the Media area then shows the cache. */
export async function fetchPresence(expectedTracks: number): Promise<Presence | null> {
  try {
    const listing = await listLibrary('', { depth: 8, limit: PRESENCE_LIMIT });
    return presenceOf(listing, expectedTracks);
  } catch {
    return null;
  }
}

/**
 * The image the player is answering from, as the device reports it. Measurements are not
 * stored in the library: every one comes from this image, which is the card's `k7.db` the
 * player applied when the card arrived. An album that says "not in the image" is described
 * by this, not by the card in the computer.
 */
export interface DeviceImage {
  /** When the image was built: the format's generation, seconds since the epoch. */
  at: string;
  rows: number;
}

export interface MediaIndexState {
  /** null until the cache has been read once. */
  index: MediaIndex | null;
  loading: boolean;
  /** The device's own answer, not the listing's: status is the poller the shell shares. */
  reachable: boolean;
  /** Whether a card is in the player. Without one its library is empty and nothing answers. */
  card: boolean;
  deviceTracks: number;
  /** The image the player answers from; null when it does not say or has none. */
  deviceImage: DeviceImage | null;
}

/**
 * The index for one visit. The cache is read once per mount and again whenever the card
 * changes — a pick mid-visit re-reads, and so does a reload, because the scope is
 * remembered (`card/session.ts`). A newly measured card therefore appears without a detour.
 */
export function useMediaIndex(): MediaIndexState {
  const { status, reachable } = useDeviceStatus();
  const scope = useCardScope();
  const [cache, setCache] = useState<Map<string, CachedMedia> | null>(null);
  const [presence, setPresence] = useState<Presence | null>(null);
  const [deviceImage, setDeviceImage] = useState<DeviceImage | null>(null);

  const tracks = status?.tracks ?? 0;
  const generation = status?.generation ?? 0;
  const hasDevice = status !== null;

  useEffect(() => {
    let cancelled = false;
    listMedia(scope)
      .then((rows) => {
        if (!cancelled) setCache(new Map(rows.map((row) => [row.path, row] as const)));
      })
      .catch(() => {
        // A store that cannot be read is an empty library, not an error page.
        if (!cancelled) setCache(new Map());
      });
    return () => {
      cancelled = true;
    };
  }, [scope]);

  useEffect(() => {
    if (!hasDevice) {
      setPresence(null);
      return;
    }
    let cancelled = false;
    void fetchPresence(tracks).then((answer) => {
      if (!cancelled) setPresence(answer);
    }).catch(() => {
      if (!cancelled) setPresence(null);
    });
    return () => {
      cancelled = true;
    };
  }, [hasDevice, tracks, generation]);

  /* Which image is answering. Its own poll: the library listing says what exists, this says
   * what the device knows about it, and the two changes are independent. */
  useEffect(() => {
    if (!hasDevice) {
      setDeviceImage(null);
      return;
    }
    let cancelled = false;
    libraryStats()
      .then((stats) => {
        if (cancelled) return;
        const image = stats.snapshot;
        setDeviceImage(
          image && image.state === 'ok' && (image.generation ?? 0) > 0
            ? {
                at: new Date((image.generation ?? 0) * 1000).toISOString(),
                rows: image.rows ?? 0,
              }
            : null,
        );
      })
      .catch(() => {
        if (!cancelled) setDeviceImage(null);
      });
    return () => {
      cancelled = true;
    };
  }, [hasDevice, generation]);

  const index = useMemo(
    () => (cache ? buildMediaIndex(cache, presence) : null),
    [cache, presence],
  );

  return {
    index,
    loading: cache === null,
    reachable,
    card: status?.card === true,
    deviceTracks: tracks,
    deviceImage,
  };
}

/** One album, by the title its route carries. */
export function findAlbum(index: MediaIndex | null, album: string): Album | null {
  return index?.albums.find((entry) => entry.album === album) ?? null;
}

/** An artist's albums, in the index's order: every shelf their name is on, compilations included. */
export function albumsOf(index: MediaIndex | null, artist: string): Album[] {
  return index?.albums.filter((entry) => entry.artists.includes(artist)) ?? [];
}
