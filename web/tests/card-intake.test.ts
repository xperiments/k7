import { describe, expect, it } from 'vitest';
import { findCardMark } from '../src/card/mark';
import { cardShapeOf, cardShapeProblem } from '../src/card/shape';

/**
 * What the user chose, before anything is measured.
 *
 * The card root is the only choice whose output can be written: one level down is the music
 * directory, one up is a folder full of cards, and both produce a run whose paths are not
 * below `music/` — measured, and with nowhere to go. These are the names that decide it.
 */
describe('the shape of a card choice', () => {
  it('takes a directory holding music/ as the card root', () => {
    const shape = cardShapeOf([
      'SDCARD/music/Artist/Album/01.mp3',
      'SDCARD/k7.card',
      'SDCARD/k7.db',
    ]);
    expect(shape).toEqual({ kind: 'card-root' });
    expect(cardShapeProblem(shape)).toBeNull();
  });

  it('reads FAT32 case as no information', () => {
    expect(cardShapeOf(['SDCARD/MUSIC/A/x.mp3'])).toEqual({ kind: 'card-root' });
    expect(cardShapeOf(['SDCARD/Music/A/x.mp3'])).toEqual({ kind: 'card-root' });
  });

  it('recognises the music directory itself, one level too deep', () => {
    const shape = cardShapeOf(['music/Artist/Album/01.mp3']);
    expect(shape).toEqual({ kind: 'music-dir' });
    expect(cardShapeProblem(shape)).toMatch(/music folder, not the card/);
  });

  it('recognises the card’s parent, one level too high', () => {
    const shape = cardShapeOf([
      'BACKUP/SDCARD/music/A/x.mp3',
      'BACKUP/OTHER/music/B/y.mp3',
      'BACKUP/notes.txt',
    ]);
    expect(shape).toEqual({ kind: 'parent', cards: ['OTHER', 'SDCARD'] });
    expect(cardShapeProblem(shape)).toMatch(/OTHER, SDCARD/);
  });

  it('refuses a directory with no music in it at all', () => {
    const shape = cardShapeOf(['RIPS/Album/01.mp3', 'RIPS/cover.jpg']);
    expect(shape).toEqual({ kind: 'no-music' });
    expect(cardShapeProblem(shape)).toMatch(/not the card/);
  });

  it('names the card when it sits more than one level down', () => {
    // `music` two or more levels below the choice means a card is in there: the name given
    // back is the path to open, not just its first segment.
    expect(cardShapeOf(['RIPS/A/music/x.mp3'])).toEqual({ kind: 'parent', cards: ['A'] });
    expect(cardShapeOf(['RIPS/A/B/music/x.mp3'])).toEqual({ kind: 'parent', cards: ['A/B'] });
  });
});

describe('the card’s own mark, found among dropped files', () => {
  it('takes the mark at the root and nothing deeper', () => {
    expect(findCardMark(['SDCARD/music/A/x.mp3', 'SDCARD/k7.card'])).toBe('SDCARD/k7.card');
    expect(findCardMark(['SDCARD/music/A/x.mp3'])).toBeNull();
    expect(findCardMark(['SDCARD/music/k7.card'])).toBeNull();
    expect(findCardMark(['k7.card'])).toBeNull();
  });
});
