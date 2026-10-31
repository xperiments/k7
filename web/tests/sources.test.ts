import { describe, expect, it } from 'vitest';
import { sourcesOf } from '../src/ui/sources';
import { sampleAnalysis } from './sampleAnalysis';
import { tagAnalysis } from '../src/domain/tagAnalysis';

const track = { name: 'a.flac', path: 'CARD/music/a.flac', size: 1, format: 'flac' as const, hash: 'x' };

describe('where each value on a row came from', () => {
  it('reports a measured analysis as measured', () => {
    const sources = sourcesOf({ ...sampleAnalysis, mood: { ...sampleAnalysis.mood, tags: { happy: 0.9, sad: 0.1, aggressive: 0.1, relaxed: 0.1, party: 0.1, danceable: 0.1 }, label: null } });
    expect(sources).toEqual({ tempo: 'measured', key: 'measured', mood: 'measured', energy: 'measured' });
  });

  it('reports a tag-only row as tags, and nothing for what no tag carried', () => {
    const analysis = tagAnalysis(track, {
      bpm: 128,
      key: { tonic: 'A', mode: 'minor', camelot: '8A' },
    });
    expect(sourcesOf(analysis)).toEqual({ tempo: 'metadata', key: 'metadata', mood: null, energy: null });
  });

  it('reports a tagged mood even when the row came from tags', () => {
    const analysis = tagAnalysis(track, {
      bpm: 128,
      key: { tonic: 'A', mode: 'minor', camelot: '8A' },
      mood: 'Happy',
      energy: 0.6,
    });
    expect(sourcesOf(analysis)).toEqual({ tempo: 'metadata', key: 'metadata', mood: 'metadata', energy: 'metadata' });
  });
});
