import { describe, expect, it } from 'vitest';
import { parseRoute, routeHref, type Route } from '../src/ui/router';

describe('the shell routes', () => {
  it('a bare hash is Process: the shell has a landing, not a not-found', () => {
    expect(parseRoute('')).toEqual({ name: 'process' });
    expect(parseRoute('#/')).toEqual({ name: 'process' });
  });

  it('reads every page the shell renders', () => {
    expect(parseRoute('#/process')).toEqual({ name: 'process' });
    expect(parseRoute('#/scan')).toEqual({ name: 'scan' });
    expect(parseRoute('#/playlists')).toEqual({ name: 'playlists' });
    expect(parseRoute('#/playlists/ask')).toEqual({ name: 'ask' });
    expect(parseRoute('#/media')).toEqual({ name: 'media' });
    expect(parseRoute('#/player')).toEqual({ name: 'player' });
    expect(parseRoute('#/settings')).toEqual({ name: 'settings' });
  });

  it('decodes ids, artists and albums, slashes included', () => {
    expect(parseRoute('#/playlists/aaaa000000000001')).toEqual({
      name: 'playlist',
      id: 'aaaa000000000001',
    });
    expect(parseRoute('#/media/album/AC%2FDC/Back%20In%20Black')).toEqual({
      name: 'album',
      artist: 'AC/DC',
      album: 'Back In Black',
    });
    expect(parseRoute('#/media/artist/Miles%20Davis')).toEqual({
      name: 'artist',
      artist: 'Miles Davis',
    });
    expect(parseRoute('#/media/video/dQw4w9WgXcQ')).toEqual({
      name: 'video',
      id: 'dQw4w9WgXcQ',
    });
  });

  it('anything else is the shell own not-found, and says which path', () => {
    expect(parseRoute('#/nope')).toEqual({ name: 'not-found', path: '/nope' });
    expect(parseRoute('#/process/extra')).toEqual({ name: 'not-found', path: '/process/extra' });
    expect(parseRoute('#/media/album/only-one-segment')).toEqual({
      name: 'not-found',
      path: '/media/album/only-one-segment',
    });
  });

  it('routeHref is the inverse of parseRoute', () => {
    const routes: Route[] = [
      { name: 'process' },
      { name: 'scan' },
      { name: 'playlists' },
      { name: 'ask' },
      { name: 'playlist', id: 'aaaa000000000001' },
      { name: 'media' },
      { name: 'album', artist: 'AC/DC', album: 'Back In Black' },
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
