import { useCallback, useLayoutEffect, useMemo, useRef, useState, type CSSProperties } from 'react';
import { ChevronDown, ChevronLeft, ChevronRight, ChevronUp } from 'lucide-react';

import { Button } from '@/components/ui/button';
import { artistOf, letterOf, type Album } from '../media/albums';
import { CassetteFront, hueOf } from './cassette';
import { plural } from './format';

/**
 * The album shelf: the cases on their short ends, spines out, the selected one turned to
 * face the reader. It is the shape a cassette library actually has — a row of spines you
 * walk along — and it gives each cover the room a 16:25 grid tile could not.
 *
 * The geometry is CSS's, not React's: the case, its four faces, the frame and the sheen are
 * in `index.css` (`.k7-shelf`), because a 3D box is a set of fixed relations, and the only
 * thing that changes per case is where it sits. This module computes that position and owns
 * the selection, the keyboard and the drag.
 *
 * Three constants below are also in the stylesheet: `--cw` (case width), `--cd` (depth) and
 * `--cg` (the gap between spines). Keeping them in one unit (rem) is what keeps the row the
 * same shape at the app's 130% base size.
 */

/** How far either side of the selection the row is built; the rest is off the shelf. */
const WINDOW = 10;

function byline(album: Album): string {
  return album.artists.length === 1 ? album.artists[0] : plural(album.artists.length, 'artist');
}

function initialOf(album: Album): string {
  return (album.album.trim().slice(0, 1) || '?').toUpperCase();
}

/** Where a case at distance `d` from the selection sits. The terms are the stylesheet's
 *  `--cw`, `--cd`, `--cg` and `--csg`, so the row scales with the case when the stage is
 *  short. */
function placeAt(distance: number): string {
  if (distance === 0) return 'translate3d(0, 0, 0)';
  const steps = Math.abs(distance) - 1;
  const dir = distance > 0 ? 1 : -1;
  const x = [
    `var(--cw) * ${0.5 * dir}`,
    `var(--cg) * ${dir}`,
    `var(--csg) * ${dir}`,
    `var(--cd) * ${dir * steps}`,
    `var(--cg) * ${dir * steps}`,
    `var(--cd) * ${0.5 * dir}`,
  ].join(' + ');
  const z = `var(--cw) * ${(-Math.min(Math.abs(distance), 8) * 0.12).toFixed(3)}`;
  return `translate3d(calc(${x}), 0, calc(${z}))`;
}

export function AlbumShelf({
  albums,
  selected,
  onSelect,
  onOpen,
}: {
  albums: Album[];
  /** Which case is turned; the page owns it because the tracks below belong to it. */
  selected: number;
  onSelect: (index: number) => void;
  /** A clicked case (not an arrow, not a letter): the caller may open its tracks. */
  onOpen?: () => void;
}) {
  const dragFrom = useRef<number | null>(null);
  const section = useRef<HTMLElement>(null);
  /* The shelf has a natural height; when the stage is shorter than it, the whole carousel —
   * cases, letters, arrows and the name — is scaled down together so nothing is pushed
   * under the header or past the player. `zoom` (not transform) so the layout shrinks too. */
  const [fit, setFit] = useState(1);
  useLayoutEffect(() => {
    const el = section.current;
    const parent = el?.parentElement;
    if (!el || !parent) return;
    const fitToStage = () => {
      if (parent.clientHeight === 0) return;
      setFit((current) => {
        const natural = el.getBoundingClientRect().height / current;
        return Math.min(1, parent.clientHeight / natural);
      });
    };
    const observer = new ResizeObserver(fitToStage);
    observer.observe(parent);
    fitToStage();
    return () => observer.disconnect();
  }, []);
  /* A drag ends with a click on whatever case the finger left; that click must not also
   * select it, or the row moves twice for one gesture. */
  const dragged = useRef(false);

  const count = albums.length;
  const at = Math.min(selected, Math.max(0, count - 1));
  const active = albums[at];

  const go = useCallback(
    (delta: number) => onSelect(Math.min(Math.max(0, at + delta), Math.max(0, count - 1))),
    [at, count, onSelect],
  );

  if (!active) return null;

  return (
    <section
      ref={section}
      style={{ zoom: fit } as CSSProperties}
      className="flex select-none flex-col items-center gap-3"
      aria-label="Albums"
    >
      <div className="flex w-full min-w-0 items-stretch gap-2">
        <div
          className="k7-shelf min-w-0 flex-1"
          role="group"
          aria-roledescription="album shelf"
          tabIndex={0}
          onKeyDown={(event) => {
            if (event.key === 'ArrowRight') {
              event.preventDefault();
              go(1);
            } else if (event.key === 'ArrowLeft') {
              event.preventDefault();
              go(-1);
            }
          }}
          onPointerDown={(event) => {
            dragFrom.current = event.clientX;
            dragged.current = false;
          }}
          onPointerUp={(event) => {
            const from = dragFrom.current;
            dragFrom.current = null;
            if (from === null) return;
            const dx = event.clientX - from;
            dragged.current = Math.abs(dx) > 10;
            if (Math.abs(dx) > 40) go(dx < 0 ? 1 : -1);
          }}
          onPointerCancel={() => {
            dragFrom.current = null;
          }}
        >
          <div className="k7-shelf-row">
            {albums.map((album, index) => {
              const distance = index - at;
              if (Math.abs(distance) > WINDOW) return null;
              const activeCase = distance === 0;
              return (
                <div
                  key={album.album}
                  className="k7-shelf-item"
                  style={{ transform: placeAt(distance) }}
                >
                  <button
                    type="button"
                    className="k7-case"
                    data-active={activeCase}
                    style={{ '--hue': `${hueOf(album.album)}deg` } as CSSProperties}
                    aria-label={`${album.album} — ${byline(album)}`}
                    aria-current={activeCase ? 'true' : undefined}
                    onClick={() => {
                      if (dragged.current) {
                        dragged.current = false;
                        return;
                      }
                      onSelect(index);
                      onOpen?.();
                    }}
                  >
                    <span className="k7-face k7-front">
                      <CaseCover album={album} />
                      <span className="k7-frame" aria-hidden="true" />
                      <span className="k7-sheen" aria-hidden="true" />
                    </span>
                    <span className="k7-face k7-back" aria-hidden="true" />
                    <span className="k7-face k7-side k7-right" aria-hidden="true" />
                    <span className="k7-face k7-side k7-left" aria-hidden="true">
                      <span className="k7-spine">
                        {album.album} · {byline(album)}
                      </span>
                      <span className="k7-frame" />
                    </span>
                  </button>
                </div>
              );
            })}
          </div>
        </div>
      </div>

      {/* The name between the two buttons: one line that says which case is turned and moves
          the row, so the tracks below start as high on the page as they can. Who made it,
          how many and what it is are the detail's line, one block down. */}
      <div className="flex items-center justify-center gap-3">
        <Button
          size="icon-sm"
          variant="outline"
          aria-label="Previous album"
          disabled={at === 0}
          onClick={() => go(-1)}
        >
          <ChevronLeft aria-hidden="true" />
        </Button>
        {/* One width for every album: the buttons are the row's controls, and a control
            that moves as the name beside it changes length is a control you have to chase. */}
        <span className="w-52 truncate text-center text-sm font-medium">{active.album}</span>
        <Button
          size="icon-sm"
          variant="outline"
          aria-label="Next album"
          disabled={at >= count - 1}
          onClick={() => go(1)}
        >
          <ChevronRight aria-hidden="true" />
        </Button>
      </div>
    </section>
  );
}

/** The letters the abecedary draws. `#` is appended only when an artist files under it. */
const LETTERS = [...'ABCDEFGHIJKLMNOPQRSTUVWXYZ'];

/**
 * The shelf's abecedary: the artists' initials down the left edge of the row, one button per
 * letter, which turns the first album by an artist that files under it.
 *
 * All twenty-six letters are drawn even where this library has none for one of them. The
 * column is an index, and an index that re-forms as the library changes is one a reader has
 * to read again every time; a letter with no artist is dimmed and inert instead of jumping
 * somewhere it does not belong.
 *
 * There is one tab stop, on the letter of the artist behind the case that is turned, and the
 * arrows walk the letters that exist. A shelf with twenty initials behind it would otherwise
 * put twenty tab stops between the search box and the row.
 */
export function AlphabetIndex({
  albums,
  selected,
  onSelect,
}: {
  albums: Album[];
  selected: number;
  onSelect: (index: number) => void;
}) {
  /* One pass over the shelf: the first album under each letter, and how many there are, which
   * is what a screen reader hears for a letter it cannot see the row behind. */
  const filing = useMemo(() => {
    const first = new Map<string, number>();
    const counts = new Map<string, number>();
    albums.forEach((album, index) => {
      const letter = letterOf(artistOf(album));
      if (!first.has(letter)) first.set(letter, index);
      counts.set(letter, (counts.get(letter) ?? 0) + 1);
    });
    return { first, counts };
  }, [albums]);

  const buttons = useRef(new Map<string, HTMLButtonElement>());
  const list = useRef<HTMLDivElement>(null);
  const current = albums[selected] ? letterOf(artistOf(albums[selected])) : null;
  /* Only the letters that lead somewhere; a dim row for an absent artist is noise. */
  const letters = [...LETTERS, ...(filing.first.has('#') ? ['#'] : [])].filter((letter) =>
    filing.first.has(letter),
  );
  if (letters.length === 0) return null;
  const stop = current !== null && filing.first.has(current) ? current : letters[0];

  /* The rail is the stage's height; when the letters are more than it, the two arrows walk
   * the list, and the buttons that would not fit are reached by that scroll instead. */
  const [overflow, setOverflow] = useState(false);
  const [atTop, setAtTop] = useState(true);
  const [atEnd, setAtEnd] = useState(true);
  useLayoutEffect(() => {
    const el = list.current;
    if (!el) return;
    const update = () => {
      setOverflow(el.scrollHeight > el.clientHeight + 1);
      setAtTop(el.scrollTop <= 0);
      setAtEnd(el.scrollHeight <= el.clientHeight + el.scrollTop + 1);
    };
    const observer = new ResizeObserver(update);
    observer.observe(el);
    el.addEventListener('scroll', update, { passive: true });
    update();
    return () => {
      observer.disconnect();
      el.removeEventListener('scroll', update);
    };
  }, [letters.length]);

  const scroll = (direction: number) => list.current?.scrollBy({ top: direction * 108 });

  const jump = (letter: string) => {
    const at = filing.first.get(letter);
    if (at !== undefined) onSelect(at);
  };

  const step = (letter: string, delta: number) => {
    const at = letters.indexOf(letter);
    const next = letters[Math.min(Math.max(0, at + delta), letters.length - 1)];
    if (next === undefined) return;
    jump(next);
    buttons.current.get(next)?.focus();
  };

  return (
    /* The full height of the stage, not of the shelf: one square plaque per letter, a
     * circle with the letter inside, like the rail's collapsed rows; the arrows walk the
     * letters that do not fit. */
    <nav
      aria-label="Artists by letter"
      className="flex h-full w-8 shrink-0 flex-col items-center gap-1"
    >
      <Button
        type="button"
        size="icon-sm"
        variant="outline"
        aria-label="Scroll the letters up"
        disabled={atTop}
        onClick={() => scroll(-1)}
        className={overflow ? '' : 'invisible'}
      >
        <ChevronUp aria-hidden="true" />
      </Button>
      <div
        ref={list}
        className="flex min-h-0 flex-1 flex-col items-center gap-1 overflow-y-auto [scrollbar-width:none] [&::-webkit-scrollbar]:hidden"
      >
        {letters.map((letter) => {
          const active = letter === current;
          return (
            <button
              key={letter}
              type="button"
              ref={(node) => {
                if (node) buttons.current.set(letter, node);
                else buttons.current.delete(letter);
              }}
              tabIndex={letter === stop ? 0 : -1}
              aria-current={active ? 'true' : undefined}
              aria-label={`${letter} — ${plural(filing.counts.get(letter) ?? 0, 'album')}`}
              className="flex size-8 shrink-0 items-center justify-center rounded-[6px] border border-foreground/45 bg-transparent p-0 leading-none outline-none transition-colors select-none hover:bg-foreground/10 focus-visible:ring-2 focus-visible:ring-ring"
              onClick={() => jump(letter)}
              onKeyDown={(event) => {
                if (event.key === 'ArrowDown') {
                  event.preventDefault();
                  step(letter, 1);
                } else if (event.key === 'ArrowUp') {
                  event.preventDefault();
                  step(letter, -1);
                } else if (event.key === 'Home') {
                  event.preventDefault();
                  step(letter, -letters.length);
                } else if (event.key === 'End') {
                  event.preventDefault();
                  step(letter, letters.length);
                }
              }}
            >
              <span
                className={`flex size-5 items-center justify-center rounded-full border border-foreground/45 text-xs leading-none ${
                  active
                    ? 'bg-secondary font-medium text-secondary-foreground'
                    : 'text-foreground'
                }`}
              >
                {letter}
              </span>
            </button>
          );
        })}
      </div>
      <Button
        type="button"
        size="icon-sm"
        variant="outline"
        aria-label="Scroll the letters down"
        disabled={atEnd}
        onClick={() => scroll(1)}
        className={overflow ? '' : 'invisible'}
      >
        <ChevronDown aria-hidden="true" />
      </Button>
    </nav>
  );
}

/** The album's face: its own art, or a tinted face with its initial. */
function CaseCover({ album }: { album: Album }) {
  return <CassetteFront cover={album.cover} initial={initialOf(album)} />;
}
