/**
 * The answer's selection: an unpaged ask that names nothing draws from across the matched
 * window, one track per artist, in library order; a name, an offset or a sort keeps its page.
 */

import { describe, expect, it } from 'vitest';

import type { McpSearch, McpTrack, SearchArgs } from '../../device/mcp';

import { PAGE, spreadAnswer, spreadApplies, spreadTracks } from './spread';

function track(artist: string, album: string, name: string): McpTrack {
  return {
    name,
    path: `${artist}/${album}/${name}`,
    size: 1000,
    format: 'mp3',
    analysis: {
      durationMs: 180_000,
      bpm: 120,
      key: '8A',
      energy: 50,
      danceability: 50,
      valence: 50,
      arousal: 50,
      mood: [],
      confidence: null,
      tagged: [],
      title: name,
      artist,
      album,
      genre: 'Rock',
    },
  };
}

/** `artists` × `each` tracks, in library order. */
function shelf(artists: readonly string[], each: number): McpTrack[] {
  return artists.flatMap((artist) =>
    Array.from({ length: each }, (_, i) => track(artist, 'Album', `${i + 1}.mp3`)),
  );
}

function answer(tracks: McpTrack[], matched = tracks.length): McpSearch {
  return { query: '', matched, truncated: matched > tracks.length, tracks };
}

describe('spreadApplies', () => {
  it('draws an unpaged ask that names nothing', () => {
    expect(spreadApplies({ genre: 'rock', limit: 10 })).toBe(true);
    expect(spreadApplies({ mood: ['relaxed'], bpmMin: 100, bpmMax: 120 })).toBe(true);
  });

  it('leaves a name, an offset and a sort to their page', () => {
    expect(spreadApplies({ query: 'nirvana', genre: 'rock' })).toBe(false);
    expect(spreadApplies({ genre: 'rock', offset: 20 })).toBe(false);
    expect(spreadApplies({ genre: 'rock', sort: 'bpm' })).toBe(false);
  });
});

describe('spreadTracks', () => {
  it('keeps one track per artist, spread across them, in library order', () => {
    const tracks = shelf(['Aaa', 'Bbb', 'Ccc', 'Ddd', 'Eee'], 4);
    const picked = spreadTracks(tracks, 4);
    expect(picked.map((one) => one.path)).toEqual([
      'Aaa/Album/1.mp3',
      'Bbb/Album/1.mp3',
      'Ddd/Album/1.mp3',
      'Eee/Album/1.mp3',
    ]);
  });

  it('fills with repeats only when the artists run out', () => {
    const tracks = shelf(['Annie Lennox'], 10);
    const picked = spreadTracks(tracks, 3);
    expect(picked.map((one) => one.path)).toEqual([
      'Annie Lennox/Album/1.mp3',
      'Annie Lennox/Album/2.mp3',
      'Annie Lennox/Album/10.mp3',
    ]);
  });

  it('keeps the first and the last artist when it spreads', () => {
    const tracks = shelf(['A', 'B', 'C', 'D', 'E', 'F'], 2);
    const picked = spreadTracks(tracks, 3);
    expect(picked.map((one) => one.path)).toEqual([
      'A/Album/1.mp3',
      'D/Album/1.mp3',
      'F/Album/1.mp3',
    ]);
  });

  it('leaves a short library whole', () => {
    const tracks = shelf(['A', 'B'], 2);
    expect(spreadTracks(tracks, 10)).toEqual(tracks);
  });

  it('takes the path folder as the artist when a row has no tag', () => {
    const untagged: McpTrack[] = [
      { name: 'a.mp3', path: 'Folder One/a.mp3', size: 1, format: 'mp3', analysis: null },
      { name: 'b.mp3', path: 'Folder Two/b.mp3', size: 1, format: 'mp3', analysis: null },
    ];
    expect(spreadTracks(untagged, 2)).toEqual(untagged);
  });
});

describe('spreadAnswer', () => {
  it('reads the window and draws the answer from it', async () => {
    const calls: SearchArgs[] = [];
    const rows = shelf(Array.from({ length: 30 }, (_, i) => `Artist ${i}`), 4);
    const search = async (args: SearchArgs): Promise<McpSearch> => {
      calls.push(args);
      return answer(rows, 120);
    };

    const found = await spreadAnswer(search, { genre: 'rock', limit: 10 });
    expect(calls[0]).toEqual({ genre: 'rock', limit: PAGE, offset: 0 });
    expect(found.matched).toBe(120);
    expect(found.truncated).toBe(true);
    expect(found.tracks).toHaveLength(10);
    expect(new Set(found.tracks.map((one) => one.analysis?.artist)).size).toBe(10);
  });

  it('reads a second page when the matches ran past the first', async () => {
    const calls: SearchArgs[] = [];
    const first = shelf(Array.from({ length: 25 }, (_, i) => `A ${i}`), 4);
    const second = shelf(Array.from({ length: 25 }, (_, i) => `B ${i}`), 4);
    const search = async (args: SearchArgs): Promise<McpSearch> => {
      calls.push(args);
      return answer((args.offset ?? 0) === 0 ? first : second, 250);
    };

    const found = await spreadAnswer(search, { genre: 'rock', limit: 10 });
    expect(calls.map((args) => args.offset)).toEqual([0, PAGE]);
    expect(found.tracks).toHaveLength(10);
    expect(found.tracks[0].path.startsWith('A 0/')).toBe(true);
    expect(found.tracks[9].path.startsWith('B 24/')).toBe(true);
  });

  it('runs one search, as asked, for a name or a page', async () => {
    const calls: SearchArgs[] = [];
    const rows = shelf(['Nirvana'], 3);
    const search = async (args: SearchArgs): Promise<McpSearch> => {
      calls.push(args);
      return answer(rows);
    };

    const named = await spreadAnswer(search, { query: 'nirvana', limit: 3 });
    expect(calls).toEqual([{ query: 'nirvana', limit: 3 }]);
    expect(named.tracks).toEqual(rows);

    const paged = await spreadAnswer(search, { genre: 'rock', offset: 20, limit: 3 });
    expect(calls[1]).toEqual({ genre: 'rock', offset: 20, limit: 3 });
    expect(paged.tracks).toEqual(rows);
  });

  it('takes the source default when the Intent names no count', async () => {
    const rows = shelf(Array.from({ length: 60 }, (_, i) => `Artist ${i}`), 2);
    const search = async (): Promise<McpSearch> => answer(rows, 120);
    const found = await spreadAnswer(search, { genre: 'rock' });
    expect(found.tracks).toHaveLength(50);
  });

  it('leaves fewer matches than the count whole', async () => {
    const rows = shelf(['A', 'B'], 1);
    const search = async (): Promise<McpSearch> => answer(rows);
    const found = await spreadAnswer(search, { genre: 'rock', limit: 10 });
    expect(found.tracks).toEqual(rows);
    expect(found.truncated).toBe(false);
  });
});
