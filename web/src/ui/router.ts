import { useSyncExternalStore } from 'react';

/**
 * The front door's routes: hashes inside one page, so the device keeps serving one file
 * and answering 404 for everything it does not know (ADR 0052).
 *
 * A route is data, not a component tree: `parseRoute` turns the hash into one of the
 * shapes the shell can render, and `useRoute` subscribes to it. Navigation is a hash
 * change, which the browser already treats as history: back and forward work, and a
 * reload lands on the same place.
 */

export type Route =
  | { name: 'prepare' }
  | { name: 'playlists' }
  | { name: 'playlist'; id: string; ask?: boolean }
  | { name: 'media' }
  | { name: 'drive'; path: string }
  | { name: 'album'; album: string }
  | { name: 'artist'; artist: string }
  | { name: 'video'; id: string }
  | { name: 'player' }
  | { name: 'settings' }
  | { name: 'not-found'; path: string };

/** One sidebar item: the page's label, where it goes, and the routes it is the page of. */
export interface NavItem {
  label: string;
  href: string;
  names: Route['name'][];
}

/** One sidebar group: the area, and its pages. */
export interface NavGroup {
  id: string;
  label: string;
  items: NavItem[];
}

function decode(segment: string): string {
  try {
    return decodeURIComponent(segment);
  } catch {
    return segment;
  }
}

/** One route per known shape; anything else is the shell's own not-found. */
export function parseRoute(hash: string): Route {
  const path = hash.replace(/^#/, '');
  const parts = path.split('/').filter((p) => p.length > 0);
  if (parts.length === 0) return { name: 'prepare' };

  switch (parts[0]) {
    case 'prepare':
      return parts.length === 1 ? { name: 'prepare' } : { name: 'not-found', path };
    case 'player':
      return parts.length === 1 ? { name: 'player' } : { name: 'not-found', path };
    case 'settings':
      return parts.length === 1 ? { name: 'settings' } : { name: 'not-found', path };
    case 'playlists':
      if (parts.length === 1) return { name: 'playlists' };
      /* The IA Agent has no page of its own any more: its old address lands on the list of
       * lists, and the conversation lives in an editor's column beside the files (ADR 0093). */
      if (parts.length === 2 && parts[1] === 'ask') return { name: 'playlists' };
      if (parts.length === 2) return { name: 'playlist', id: decode(parts[1]) };
      if (parts.length === 3 && parts[2] === 'ask') {
        return { name: 'playlist', id: decode(parts[1]), ask: true };
      }
      return { name: 'not-found', path };
    case 'media':
      if (parts.length === 1) return { name: 'media' };
      /* Drive folders are paths, not ids: every segment after `drive` is one level. The
       * root is `#/media/drive`, with no segments at all. */
      if (parts[1] === 'drive') {
        return { name: 'drive', path: parts.slice(2).map(decode).join('/') };
      }
      if (parts.length === 3 && parts[1] === 'album')
        return { name: 'album', album: decode(parts[2]) };
      if (parts.length === 3 && parts[1] === 'artist')
        return { name: 'artist', artist: decode(parts[2]) };
      if (parts.length === 3 && parts[1] === 'video')
        return { name: 'video', id: decode(parts[2]) };
      return { name: 'not-found', path };
    default:
      return { name: 'not-found', path };
  }
}

/** The hash a route is addressed by; the inverse of `parseRoute`. */
export function routeHref(route: Route): string {
  switch (route.name) {
    case 'playlists':
      return '#/playlists';
    case 'playlist':
      return route.ask
        ? `#/playlists/${encodeURIComponent(route.id)}/ask`
        : `#/playlists/${encodeURIComponent(route.id)}`;
    case 'media':
      return '#/media';
    case 'drive':
      // One segment per folder; the card's own names cannot carry a slash, so the address
      // and the path are the same levels.
      return route.path
        ? `#/media/drive/${route.path.split('/').map(encodeURIComponent).join('/')}`
        : '#/media/drive';
    case 'album':
      return `#/media/album/${encodeURIComponent(route.album)}`;
    case 'artist':
      return `#/media/artist/${encodeURIComponent(route.artist)}`;
    case 'video':
      return `#/media/video/${encodeURIComponent(route.id)}`;
    case 'player':
      return '#/player';
    case 'settings':
      return '#/settings';
    default:
      return '#/prepare';
  }
}

/**
 * The sidebar and the breadcrumb, from one definition.
 *
 * The group label is the area; the item label is the page of a top-level route. The
 * album/artist/Video/Drive pages and the playlist Editor keep their own names, and the
 * Media Sources pages hide the area because the page already says which view it is.
 */
export const NAV_GROUPS: NavGroup[] = [
  {
    id: 'sources',
    label: 'Media Sources',
    items: [
      { label: 'Sources', href: routeHref({ name: 'prepare' }), names: ['prepare'] },
      {
        label: 'Albums',
        href: routeHref({ name: 'media' }),
        names: ['media', 'album', 'artist', 'video', 'drive'],
      },
    ],
  },
  {
    id: 'playlists',
    label: 'Playlists',
    items: [
      {
        label: 'Playlists',
        href: routeHref({ name: 'playlists' }),
        names: ['playlists', 'playlist'],
      },
    ],
  },
  {
    id: 'player',
    label: 'Player',
    items: [{ label: 'Player', href: routeHref({ name: 'player' }), names: ['player'] }],
  },
];

/** The area and page the header shows for a route. */
export function routeTitle(route: Route): { area: string | null; page: string } {
  if (route.name === 'settings') return { area: null, page: 'Settings' };
  if (route.name === 'not-found') return { area: null, page: 'Not found' };

  const group = NAV_GROUPS.find((each) =>
    each.items.some((item) => item.names.includes(route.name)),
  );
  const item = group?.items.find((each) => each.names.includes(route.name));
  if (group === undefined || item === undefined) return { area: null, page: 'Not found' };

  return {
    area: group.label,
    page: subPage(route) ?? item.label,
  };
}

/** A page whose route is below a destination and names itself. */
function subPage(route: Route): string | null {
  switch (route.name) {
    case 'playlist':
      return 'Editor';
    case 'album':
      return route.album;
    case 'artist':
      return route.artist;
    case 'video':
      return 'Video';
    case 'drive':
      // The whole drive is the page; a path in the address only says which folder to open.
      return 'Drive';
    case 'player':
      // A destination whose page says what it shows; the item stays "Player".
      return 'Now playing';
    default:
      return null;
  }
}

export function currentRoute(): Route {
  return parseRoute(window.location.hash);
}

/** Change the address. `replace` keeps the previous entry out of history (a redirect). */
export function navigate(hash: string, options?: { replace?: boolean }): void {
  if (window.location.hash === hash) return;
  if (options?.replace) {
    const url = `${window.location.pathname}${window.location.search}${hash}`;
    window.history.replaceState(null, '', url);
    window.dispatchEvent(new HashChangeEvent('hashchange'));
  } else {
    window.location.hash = hash;
  }
}

function subscribe(onChange: () => void): () => void {
  window.addEventListener('hashchange', onChange);
  return () => window.removeEventListener('hashchange', onChange);
}

/** The current route, re-rendered on every hash change. */
export function useRoute(): Route {
  return parseRoute(
    useSyncExternalStore(
      subscribe,
      () => window.location.hash,
      () => '',
    ),
  );
}
