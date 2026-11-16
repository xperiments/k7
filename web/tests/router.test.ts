import { describe, expect, it } from 'vitest';
import { NAV_GROUPS, parseRoute, routeHref, routeTitle, type Route } from '../src/ui/router';

describe('the shell routes', () => {
  it('a bare hash is Prepare: the shell has a landing, not a not-found', () => {
    expect(parseRoute('')).toEqual({ name: 'prepare' });
    expect(parseRoute('#/')).toEqual({ name: 'prepare' });
  });

  it('reads every page the shell renders', () => {
    expect(parseRoute('#/prepare')).toEqual({ name: 'prepare' });
    // `#/scan` is gone: reading a card is what Prepare does, and its dock is the drop zone.
    expect(parseRoute('#/scan')).toEqual({ name: 'not-found', path: '/scan' });
    expect(parseRoute('#/playlists')).toEqual({ name: 'playlists' });
    /* The IA Agent has no page any more: its old address lands on the list of lists, and the
     * conversation is a tab of one editor (ADR 0093). */
    expect(parseRoute('#/playlists/ask')).toEqual({ name: 'playlists' });
    expect(parseRoute('#/media')).toEqual({ name: 'media' });
    expect(parseRoute('#/media/drive')).toEqual({ name: 'drive', path: '' });
    expect(parseRoute('#/player')).toEqual({ name: 'player' });
    expect(parseRoute('#/settings')).toEqual({ name: 'settings' });
  });

  it('decodes ids, artists and albums, slashes included', () => {
    expect(parseRoute('#/playlists/aaaa000000000001')).toEqual({
      name: 'playlist',
      id: 'aaaa000000000001',
    });
    // The Ask tab is gone; its old address is the editor, not a 404.
    expect(parseRoute('#/playlists/aaaa000000000001/ask')).toEqual({
      name: 'playlist',
      id: 'aaaa000000000001',
    });
    expect(parseRoute('#/media/album/AC%2FDC%20-%20Back%20In%20Black')).toEqual({
      name: 'album',
      album: 'AC/DC - Back In Black',
    });
    expect(parseRoute('#/media/artist/Miles%20Davis')).toEqual({
      name: 'artist',
      artist: 'Miles Davis',
    });
    expect(parseRoute('#/media/video/dQw4w9WgXcQ')).toEqual({
      name: 'video',
      id: 'dQw4w9WgXcQ',
    });
    // A drive path is made of segments, and a segment can still carry a slash of its own.
    expect(parseRoute('#/media/drive/Artist/Album')).toEqual({
      name: 'drive',
      path: 'Artist/Album',
    });
    expect(parseRoute('#/media/drive/AC%2FDC/Back%20In%20Black')).toEqual({
      name: 'drive',
      path: 'AC/DC/Back In Black',
    });
  });

  it('anything else is the shell own not-found, and says which path', () => {
    expect(parseRoute('#/nope')).toEqual({ name: 'not-found', path: '/nope' });
    expect(parseRoute('#/prepare/extra')).toEqual({ name: 'not-found', path: '/prepare/extra' });
    expect(parseRoute('#/media/album/a/b')).toEqual({
      name: 'not-found',
      path: '/media/album/a/b',
    });
  });

  it('routeHref is the inverse of parseRoute', () => {
    const routes: Route[] = [
      { name: 'prepare' },
      { name: 'playlists' },
      { name: 'playlist', id: 'aaaa000000000001' },
      { name: 'media' },
      { name: 'drive', path: '' },
      { name: 'drive', path: 'Artist/Album' },
      { name: 'drive', path: 'AC/DC/Back In Black' },
      { name: 'album', album: 'AC/DC - Back In Black' },
      { name: 'artist', artist: 'Miles Davis' },
      { name: 'video', id: 'dQw4w9WgXcQ' },
      { name: 'player' },
      { name: 'settings' },
    ];
    for (const route of routes) {
      expect(parseRoute(routeHref(route))).toEqual(route);
    }
  });
});

describe('the sidebar and the breadcrumb', () => {
  it('takes the page from the sidebar item and the area from its group', () => {
    expect(routeTitle(parseRoute('#/prepare'))).toEqual({
      area: 'Media Sources',
      page: 'Sources',
    });
    expect(routeTitle(parseRoute('#/playlists'))).toEqual({
      area: 'Playlists',
      page: 'Playlists',
    });
    expect(routeTitle(parseRoute('#/player'))).toEqual({
      area: 'Player',
      page: 'Now playing',
    });
  });

  it('names the Media Sources pages under their area', () => {
    expect(routeTitle(parseRoute('#/media'))).toEqual({
      area: 'Media Sources',
      page: 'Albums',
    });
    expect(routeTitle(parseRoute('#/media/album/AC%2FDC'))).toEqual({
      area: 'Media Sources',
      page: 'AC/DC',
    });
    expect(routeTitle(parseRoute('#/media/artist/Miles%20Davis'))).toEqual({
      area: 'Media Sources',
      page: 'Miles Davis',
    });
    expect(routeTitle(parseRoute('#/media/drive/Artist/Album'))).toEqual({
      area: 'Media Sources',
      page: 'Drive',
    });
    expect(routeTitle(parseRoute('#/media/video/dQw4w9WgXcQ'))).toEqual({
      area: 'Media Sources',
      page: 'Video',
    });
  });

  it('keeps the sub-pages and the shell pages named', () => {
    expect(routeTitle(parseRoute('#/playlists/aaaa000000000001'))).toEqual({
      area: 'Playlists',
      page: 'Editor',
    });
    expect(routeTitle(parseRoute('#/settings'))).toEqual({ area: null, page: 'Settings' });
    expect(routeTitle(parseRoute('#/nope'))).toEqual({ area: null, page: 'Not found' });
  });

  it('every sidebar item opens its area and the right page', () => {
    // The rail reads Player, then the card's sources, then the lists.
    expect(NAV_GROUPS.map((group) => group.label)).toEqual([
      'Player',
      'Media Sources',
      'Playlists',
    ]);
    for (const group of NAV_GROUPS) {
      for (const item of group.items) {
        const title = routeTitle(parseRoute(item.href));
        // The Player's page says what it shows rather than repeating the item.
        expect(title.area).toBe(group.label);
        expect(title.page).toBe(item.names[0] === 'player' ? 'Now playing' : item.label);
      }
    }
  });
});
