import { describe, expect, it } from 'vitest';
import {
  MAX_VIDEOS,
  VideoError,
  instanceBase,
  isAbort,
  mapSearchResults,
  watchUrl,
} from './youtube';

/**
 * The instance's answer is the contract: it is a third-party shape, it changes, and a
 * playlist or a channel turns up in a `type=video` answer often enough that dropping it
 * is normal rather than exceptional. These fixtures are the shape, not a network call.
 */

const valid = {
  type: 'video',
  videoId: 'dQw4w9WgXcQ',
  title: '  Never Gonna Give You Up  ',
  author: 'Rick Astley',
  lengthSeconds: 213,
  videoThumbnails: [
    { quality: 'medium', url: 'https://i.ytimg.com/vi/dQw4w9WgXcQ/mqdefault.jpg', width: 320 },
  ],
};

describe('mapSearchResults', () => {
  it('maps the fields the page renders, trimmed', () => {
    expect(mapSearchResults([valid])).toEqual([
      {
        id: 'dQw4w9WgXcQ',
        title: 'Never Gonna Give You Up',
        channel: 'Rick Astley',
        duration: 213,
        thumbnail: 'https://i.ytimg.com/vi/dQw4w9WgXcQ/mqdefault.jpg',
      },
    ]);
  });

  it('falls back to the author id, then to an empty channel', () => {
    const byId = mapSearchResults([{ ...valid, author: undefined, authorId: 'UC123' }]);
    expect(byId[0]?.channel).toBe('UC123');

    const byNothing = mapSearchResults([{ ...valid, author: '   ', authorId: undefined }]);
    expect(byNothing[0]?.channel).toBe('');
  });

  it('drops entries that are not a video', () => {
    const mixed = [
      null,
      'video',
      42,
      { ...valid, videoId: undefined },
      { ...valid, title: '   ' },
      { ...valid, videoId: 'not a valid id!' },
      { type: 'playlist', playlistId: 'PL123', title: 'A playlist' },
      { type: 'channel', author: 'A channel', authorId: 'UC1' },
      valid,
    ];
    expect(mapSearchResults(mixed).map((video) => video.id)).toEqual(['dQw4w9WgXcQ']);
  });

  it('keeps at most five, in the order the instance gave them', () => {
    const many = Array.from({ length: MAX_VIDEOS + 3 }, (_, index) => ({
      ...valid,
      videoId: `video${index}000`,
      title: `Video ${index}`,
    }));
    const videos = mapSearchResults(many);
    expect(videos).toHaveLength(MAX_VIDEOS);
    expect(videos[0]?.title).toBe('Video 0');
  });

  it('uses a usable thumbnail and otherwise builds one from the id', () => {
    const withBadThumb = mapSearchResults([
      { ...valid, videoThumbnails: [{ url: 'javascript:alert(1)' }, { url: 'not a url' }] },
    ]);
    expect(withBadThumb[0]?.thumbnail).toBe(
      'https://i.ytimg.com/vi/dQw4w9WgXcQ/mqdefault.jpg',
    );

    const withNoThumbs = mapSearchResults([{ ...valid, videoThumbnails: undefined }]);
    expect(withNoThumbs[0]?.thumbnail).toContain('/vi/dQw4w9WgXcQ/');
  });

  it('turns a missing or nonsensical duration into zero', () => {
    expect(mapSearchResults([{ ...valid, lengthSeconds: '213' }])[0]?.duration).toBe(0);
    expect(mapSearchResults([{ ...valid, lengthSeconds: -4 }])[0]?.duration).toBe(0);
    expect(mapSearchResults([{ ...valid, lengthSeconds: Number.NaN }])[0]?.duration).toBe(0);
  });

  it('answers nothing for a shape that is not a list', () => {
    expect(mapSearchResults({ videos: [valid] })).toEqual([]);
    expect(mapSearchResults(null)).toEqual([]);
  });
});

describe('instanceBase', () => {
  it('drops trailing slashes and keeps a path prefix', () => {
    expect(instanceBase('https://inv.example.org/')).toBe('https://inv.example.org');
    expect(instanceBase('  https://example.org/invidious/  ')).toBe(
      'https://example.org/invidious',
    );
    expect(instanceBase('http://192.168.1.4:3000')).toBe('http://192.168.1.4:3000');
  });

  it('refuses what cannot be a request', () => {
    expect(() => instanceBase('')).toThrow(VideoError);
    expect(() => instanceBase('inv.example.org')).toThrow(/not a URL/);
    expect(() => instanceBase('ftp://example.org')).toThrow(/http or https/);
  });
});

describe('isAbort', () => {
  it('recognises the error a cancelled search throws, and nothing else', () => {
    expect(isAbort(new DOMException('aborted', 'AbortError'))).toBe(true);
    expect(isAbort(Object.assign(new Error('x'), { name: 'AbortError' }))).toBe(true);
    expect(isAbort(new VideoError('down', 0))).toBe(false);
    expect(isAbort(null)).toBe(false);
  });

  it('upgrades an instance’s http thumbnail, which an https page cannot show', () => {
    const [video] = mapSearchResults([
      { ...valid, videoThumbnails: [{ url: 'http://inv.example/vi/dQw4w9WgXcQ/mq.jpg' }] },
    ]);
    expect(video.thumbnail).toBe('https://inv.example/vi/dQw4w9WgXcQ/mq.jpg');
  });

  it('opens the Video on YouTube, never in an embed', () => {
    expect(watchUrl('dQw4w9WgXcQ')).toBe('https://www.youtube.com/watch?v=dQw4w9WgXcQ');
  });
});
