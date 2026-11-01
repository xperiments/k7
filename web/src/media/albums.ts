/**
 * The album and artist index: the shape the Media area browses.
 *
 * **Names come from this browser**, not from the device's paging. The cache in
 * `xtape-media` holds the title, artist, album, genre and picture of every track this
 * browser read while the card was in the computer; the device's `library_search` pages at
 * `offset <= 199`, so indexing 5,000 tracks by paging would take ~50 requests and still be
 * partial (front door spec §4.4, corrected by the build note). What the device supplies is
 * **existence** -- the track count and generation in `/api/status`, plus one listing walked
 * for track paths -- and, when an album is opened, the measurements for its rows.
 *
 * The rules that follow from that:
 *
 * 1. An album is a group of cache rows; an artist is a group of albums. No cache row, no
 *    album -- which is why the empty state says "measure the card once".
 * 2. Existence decides what counts. When the device listed *the whole* library, a cache row
 *    whose track is no longer on the card is left out: the album does not claim a file that
 *    is gone. When the listing was partial (it caps at 200 entries), the cache is shown
 *    whole, because absence from a partial listing means nothing.
 * 3. The tracks the device listed and this browser has no cache row for are **not
 *    measured** here. They are reported as such and link to Process, never guessed at.
 */

import { useEffect, useMemo, useState } from 'react';
import { listLibrary } from '../device/mcp';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { allMedia, type CachedCover, type CachedMedia } from '../storage/media';

export const UNKNOWN_ARTIST = 'Unknown artist';
export const UNKNOWN_ALBUM = 'Unknown album';

/** `library_list` refuses a limit above this, so one call cannot list a large library. */
const PRESENCE_LIMIT = 200;

export interface Album {
  /** Display name *and* identity: the route carries these two strings. */
  artist: string;
  album: string;
  /** The cache rows, in path order. */
  rows: CachedMedia[];
  tracks: number;
  cover: CachedCover | null;
}

export interface ArtistEntry {
  name: string;
  albums: number;
  tracks: number;
}

/** A track the device listed, with the name the listing gave it. */
export interface DeviceTrack {
  name: string;
  path: string;
}

export interface Presence {
  /** Every track path the device listed. */
  paths: ReadonlySet<string>;
  tracks: DeviceTrack[];
  /** True when the listing covered the whole library, so absence means the track is gone. */
  complete: boolean;
}

export interface MediaIndex {
  albums: Album[];
  artists: ArtistEntry[];
  /** Device tracks with no cache row: measured nowhere this browser knows. */
  unmeasured: DeviceTrack[];
  presence: Presence | null;
}

function clean(value: string | undefined): string {
  return typeof value === 'string' ? value.trim() : '';
}

/** An album's name and artist, with the fallbacks that also serve as their identity. */
export function albumOf(row: Pick<CachedMedia, 'artist' | 'album'>): {
  artist: string;
  album: string;
} {
  return {
    artist: clean(row.artist) || UNKNOWN_ARTIST,
    album: clean(row.album) || UNKNOWN_ALBUM,
  };
}

/** The key two album routes share; `\0` cannot appear in a tag, so it cannot collide. */
export function albumKey(artist: string, album: string): string {
  return `${artist}\u0000${album}`;
}

function byText(left: string, right: string): number {
  return left.localeCompare(right, undefined, { sensitivity: 'base' });
}

function byPath(left: CachedMedia, right: CachedMedia): number {
  return left.path.localeCompare(right.path);
}

/** Group the cache into albums and artists. Pure, so the rule above is testable by reading. */
export function buildMediaIndex(
  cache: ReadonlyMap<string, CachedMedia>,
  presence: Presence | null,
): MediaIndex {
  const rows = [...cache.values()].filter(
    (row) => !(presence?.complete ?? false) || presence?.paths.has(row.path),
  );

  const groups = new Map<string, Album>();
  for (const row of rows) {
    const names = albumOf(row);
    const key = albumKey(names.artist, names.album);
    const album = groups.get(key);
    if (album) {
      album.rows.push(row);
    } else {
      groups.set(key, {
        artist: names.artist,
        album: names.album,
        rows: [row],
        tracks: 0,
        cover: null,
      });
    }
  }

  const albums = [...groups.values()];
  for (const album of albums) {
    album.rows.sort(byPath);
    album.tracks = album.rows.length;
    album.cover = album.rows.find((row) => row.cover)?.cover ?? null;
  }
  albums.sort((left, right) => byText(left.artist, right.artist) || byText(left.album, right.album));

  const artists = new Map<string, ArtistEntry>();
  for (const album of albums) {
    const artist = artists.get(album.artist) ?? { name: album.artist, albums: 0, tracks: 0 };
    artist.albums += 1;
    artist.tracks += album.tracks;
    artists.set(album.artist, artist);
  }

  const unmeasured = (presence?.tracks ?? [])
    .filter((track) => !cache.has(track.path))
    .sort((left, right) => byText(left.path, right.path));

  return {
    albums,
    artists: [...artists.values()].sort((left, right) => byText(left.name, right.name)),
    unmeasured,
    presence,
  };
}

/**
 * One listing, walked for track paths. `expectedTracks` comes from `/api/status`, so the
 * listing can say whether it covered the library: the device caps a call at 200 entries,
 * and a count that matches the Library's own track count is the only proof of completeness.
 *
 * `null` means the device did not answer -- the Media area then shows the cache alone and
 * says so, rather than reporting every cached track as missing.
 */
export async function fetchPresence(expectedTracks: number): Promise<Presence | null> {
  let listing;
  try {
    listing = await listLibrary('', { depth: 8, limit: PRESENCE_LIMIT });
  } catch {
    return null;
  }

  const paths = new Set<string>();
  const tracks: DeviceTrack[] = [];
  for (const entry of listing.entries ?? []) {
    if (entry.type !== 'track') continue;
    if (paths.has(entry.path)) continue;
    paths.add(entry.path);
    tracks.push({ name: entry.name, path: entry.path });
  }

  // A listing with no tracks proves nothing: the card may be out of the player, where the
  // library is empty and this browser's cache is still the only record of what was measured.
  const complete =
    tracks.length > 0 && !listing.truncated && tracks.length === expectedTracks;

  return { paths, tracks: tracks.sort((left, right) => byText(left.path, right.path)), complete };
}

export interface MediaIndexState {
  /** null until the cache has been read once. */
  index: MediaIndex | null;
  loading: boolean;
  /** The device's own answer, not the listing's: status is the poller the shell shares. */
  reachable: boolean;
  deviceTracks: number;
}

/**
 * The index for one visit. The cache is read once per mount -- navigating away and back
 * re-reads it, which is what makes a newly measured card appear.
 */
export function useMediaIndex(): MediaIndexState {
  const { status, reachable } = useDeviceStatus();
  const [cache, setCache] = useState<Map<string, CachedMedia> | null>(null);
  const [presence, setPresence] = useState<Presence | null>(null);

  const tracks = status?.tracks ?? 0;
  const generation = status?.generation ?? 0;
  const hasDevice = status !== null;

  useEffect(() => {
    let cancelled = false;
    allMedia()
      .then((rows) => {
        if (!cancelled) setCache(rows);
      })
      .catch(() => {
        // A store that cannot be read is an empty library, not an error page.
        if (!cancelled) setCache(new Map());
      });
    return () => {
      cancelled = true;
    };
  }, []);

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

  const index = useMemo(
    () => (cache ? buildMediaIndex(cache, presence) : null),
    [cache, presence],
  );

  return { index, loading: cache === null, reachable, deviceTracks: tracks };
}

/** One album, by the two strings its route carries. */
export function findAlbum(index: MediaIndex | null, artist: string, album: string): Album | null {
  return index?.albums.find((entry) => entry.artist === artist && entry.album === album) ?? null;
}

/** An artist's albums, in the index's order (artist, then album). */
export function albumsOf(index: MediaIndex | null, artist: string): Album[] {
  return index?.albums.filter((entry) => entry.artist === artist) ?? [];
}
