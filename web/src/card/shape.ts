/**
 * What the user actually chose, judged from the paths below it.
 *
 * The App's whole output hangs off the **card root**: the mark that names the card, the
 * Update file the player consumes, and the `music/` directory the Library is built from. A
 * picker that lands one level too high or too low produces a run that cannot be written to
 * the card at all — every Track falls outside `music/` — so the shape is decided at intake
 * and a wrong one is said out loud instead of measured.
 *
 * Both scanners hand over paths that start with the chosen directory's own name
 * (`SDCARD/music/…`, `music/Artist/…`), exactly as `webkitRelativePath` does, so the shape is
 * read from where the `music` segment sits:
 *
 *   1   the chosen directory holds `music/`          → the card root
 *   0   the chosen directory *is* `music/`           → the music directory, one level down
 *   2+  `music/` is inside a child of the choice     → the card's parent, one level up
 *   –   no `music/` in the walk at all               → not a card
 *
 * `music` is matched without regard to case: a card is FAT32, where `MUSIC` is `music`
 * (`card/mark.ts` K1, and `snapshotPath` matches the same segment the same way).
 */

export type CardShape =
  /** The chosen directory is the card root. */
  | { kind: 'card-root' }
  /** The chosen directory is the card's `music` directory. */
  | { kind: 'music-dir' }
  /** `music/` lives one level down; `cards` names the directories worth choosing instead. */
  | { kind: 'parent'; cards: string[] }
  /** No `music` directory anywhere below the choice. */
  | { kind: 'no-music' };

const MUSIC = 'music';

function segments(path: string): string[] {
  return path.split('/').filter((segment) => segment !== '');
}

/** Where the `music` segment sits in the path, or `null` when the path has none. */
function musicIndex(path: string): number | null {
  const parts = segments(path);
  for (let at = 0; at < parts.length; at += 1) {
    if (parts[at].toLowerCase() === MUSIC) return at;
  }
  return null;
}

/**
 * The shape of a choice, from every path found below it. Nothing is read: a wrong selection
 * is a property of the names, so the answer costs one pass over the walk that already happened.
 */
export function cardShapeOf(paths: readonly string[]): CardShape {
  let deepest = -1;
  const cards = new Set<string>();

  for (const path of paths) {
    const at = musicIndex(path);
    if (at === null) continue;
    if (at === 1) return { kind: 'card-root' };
    if (at === 0) deepest = Math.max(deepest, 0);
    else if (at >= 2) {
      deepest = Math.max(deepest, at);
      // The card is the directory that *holds* music, named relative to the choice, so a
      // folder nested two deep still gives the user the path to open.
      const parts = segments(path);
      const card = parts.slice(1, at).join('/');
      if (card) cards.add(card);
    }
  }

  if (deepest === 0) return { kind: 'music-dir' };
  if (deepest >= 2) return { kind: 'parent', cards: [...cards].sort() };
  return { kind: 'no-music' };
}

/**
 * The sentence to show when the choice is not a card root. `null` means it is one.
 *
 * Each case says what was chosen and what to do instead, because the three are one gesture
 * apart and "this is not a card" alone would leave the user guessing which way to move.
 */
export function cardShapeProblem(shape: CardShape): string | null {
  switch (shape.kind) {
    case 'card-root':
      return null;
    case 'music-dir':
      return (
        'That is the card’s music folder, not the card. Choose the folder that holds ' +
        '`music` and `k7.db` — one level up — so the image can be written back to the card.'
      );
    case 'parent':
      return (
        `That folder holds ${shape.cards.length === 1 ? 'a card' : 'cards'} below it` +
        `${shape.cards.length > 0 ? ` (${shape.cards.slice(0, 3).join(', ')}${shape.cards.length > 3 ? ', …' : ''})` : ''}. ` +
        'Choose the card itself: the folder that holds `music`.'
      );
    case 'no-music':
      return (
        'No `music` folder in there, so it is not the card. Choose the SD card’s root — ' +
        'the folder that holds `music`.'
      );
  }
}
