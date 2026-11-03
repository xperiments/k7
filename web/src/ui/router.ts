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
  | { name: 'process' }
  | { name: 'scan' }
  | { name: 'playlists' }
  | { name: 'playlist'; id: string }
  | { name: 'ask' }
  | { name: 'media' }
  | { name: 'album'; artist: string; album: string }
  | { name: 'artist'; artist: string }
  | { name: 'video'; id: string }
  | { name: 'player' }
  | { name: 'settings' }
  | { name: 'not-found'; path: string };

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
  if (parts.length === 0) return { name: 'process' };

  switch (parts[0]) {
    case 'process':
      return parts.length === 1 ? { name: 'process' } : { name: 'not-found', path };
    case 'scan':
      return parts.length === 1 ? { name: 'scan' } : { name: 'not-found', path };
    case 'player':
      return parts.length === 1 ? { name: 'player' } : { name: 'not-found', path };
    case 'settings':
      return parts.length === 1 ? { name: 'settings' } : { name: 'not-found', path };
    case 'playlists':
      if (parts.length === 1) return { name: 'playlists' };
      if (parts.length === 2 && parts[1] === 'ask') return { name: 'ask' };
      if (parts.length === 2) return { name: 'playlist', id: decode(parts[1]) };
      return { name: 'not-found', path };
    case 'media':
      if (parts.length === 1) return { name: 'media' };
      if (parts.length === 4 && parts[1] === 'album')
        return { name: 'album', artist: decode(parts[2]), album: decode(parts[3]) };
      if (parts.length === 3 && parts[1] === 'artist')
        return { name: 'artist', artist: decode(parts[2]) };
      if (parts.length === 3 && parts[1] === 'video')
        return { name: 'video', id: decode(parts[2]) };
      return { name: 'not-found', path };
    default:
      return { name: 'not-found', path };
  }
}

/** The area a route belongs to, or null for the shell's own pages (Settings). */
export function routeArea(route: Route): 'Card' | 'Playlists' | 'Media' | 'Player' | null {
  switch (route.name) {
    case 'process':
    case 'scan':
      return 'Card';
    case 'playlists':
    case 'playlist':
    case 'ask':
      return 'Playlists';
    case 'media':
    case 'album':
    case 'artist':
    case 'video':
      return 'Media';
    case 'player':
      return 'Player';
    default:
      return null;
  }
}

/** The page's name for the breadcrumb: the area says where, this says what. */
export function routePage(route: Route): string {
  switch (route.name) {
    case 'process':
      return 'Process';
    case 'scan':
      return 'Scan';
    case 'playlists':
      return 'Playlists';
    case 'playlist':
      return 'Editor';
    case 'ask':
      return 'Ask';
    case 'media':
      return 'Albums & artists';
    case 'album':
      return route.album;
    case 'artist':
      return route.artist;
    case 'video':
      return 'Video';
    case 'player':
      return 'Now playing';
    case 'settings':
      return 'Settings';
    default:
      return 'Not found';
  }
}

/** The hash a route is addressed by; the inverse of `parseRoute`. */
export function routeHref(route: Route): string {
  switch (route.name) {
    case 'scan':
      return '#/scan';
    case 'playlists':
      return '#/playlists';
    case 'playlist':
      return `#/playlists/${encodeURIComponent(route.id)}`;
    case 'ask':
      return '#/playlists/ask';
    case 'media':
      return '#/media';
    case 'album':
      return `#/media/album/${encodeURIComponent(route.artist)}/${encodeURIComponent(route.album)}`;
    case 'artist':
      return `#/media/artist/${encodeURIComponent(route.artist)}`;
    case 'video':
      return `#/media/video/${encodeURIComponent(route.id)}`;
    case 'player':
      return '#/player';
    case 'settings':
      return '#/settings';
    default:
      return '#/process';
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
