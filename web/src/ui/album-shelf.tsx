import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
  type CSSProperties,
  type PointerEvent as ReactPointerEvent,
} from 'react';
import { ChevronDown, ChevronLeft, ChevronRight, ChevronUp } from 'lucide-react';

import { Button } from '@/components/ui/button';
import type { Album } from '../media/albums';
import { hueOf } from './album-art';
import { CassetteFront } from './cassette';
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
/** The same geometry as `--cw`, `--cd`, `--cg` and the selected gap in `index.css`, in rem:
 *  the selected case's neighbour, then each spine after it. */
const FIRST_STEP = 9.7;
const SPINE_STEP = 3.25;

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
  const section = useRef<HTMLElement>(null);
  /* The shelf has a natural height; when the stage is shorter than it, the whole carousel —
   * cases, letters, arrows and the name — is scaled down together so nothing is pushed
   * under the header or past the player. `zoom` (not transform) so the layout shrinks too. */
  const [fit, setFit] = useState(1);
  /* Which case the pointer is on, read from the flat hit layer below. It is deliberately
   * not the case's own :hover: the case turns and rises while the pointer stays put, and a
   * hover that moved with the 3D box would keep dropping out from under the pointer. */
  const [hovered, setHovered] = useState<number | null>(null);
  useLayoutEffect(() => {
    const el = section.current;
    const parent = el?.parentElement;
    if (!el || !parent) return;
    const fitToStage = () => {
      if (parent.clientHeight === 0) return;
      setFit((current) => {
        const natural = el.getBoundingClientRect().height / current;
        return Math.min(1.4, parent.clientHeight / natural);
      });
    };
    const observer = new ResizeObserver(fitToStage);
    observer.observe(parent);
    fitToStage();
    return () => observer.disconnect();
  }, []);
  /* The pointer drags the row *through* the cases: every case crossed turns the selection,
   * so the 3D follows the hand; the release lands on the nearest one. A press that never
   * moved is a click on the case. */
  const dragStart = useRef<number | null>(null);
  const dragStartAt = useRef(0);
  const dragTarget = useRef(0);
  const dragRem = useRef(16);
  const dragged = useRef(false);
  const stepsFor = (dx: number) => {
    const r = -dx / dragRem.current;
    if (Math.abs(r) < FIRST_STEP / 2) return 0;
    return Math.sign(r) * (1 + Math.round((Math.abs(r) - FIRST_STEP) / SPINE_STEP));
  };

  /* The pointer may be released outside the shelf, outside the window, or not at all; one
   * end path serves the shelf's pointerup, a window-level pointerup/cancel/blur and a lost
   * capture, so a release can never leave the row in hand. */
  const endRef = useRef<(clientX: number | null) => void>(() => {});
  endRef.current = (clientX) => {
    if (dragStart.current === null) return;
    const from = dragStart.current;
    dragStart.current = null;
    if (clientX === null) return;
    const target = Math.min(
      Math.max(0, dragStartAt.current + stepsFor(clientX - from)),
      Math.max(0, count - 1),
    );
    if (target !== dragTarget.current) {
      dragTarget.current = target;
      onSelect(target);
    }
  };
  useEffect(() => {
    const end = (event: PointerEvent) => endRef.current(event.clientX);
    const cancel = () => endRef.current(null);
    window.addEventListener('pointerup', end);
    window.addEventListener('pointercancel', cancel);
    window.addEventListener('blur', cancel);
    return () => {
      window.removeEventListener('pointerup', end);
      window.removeEventListener('pointercancel', cancel);
      window.removeEventListener('blur', cancel);
    };
  }, []);

  const count = albums.length;
  const at = Math.min(selected, Math.max(0, count - 1));
  const active = albums[at];
  const hoverSide =
    hovered === null ? undefined : hovered < at ? 'left' : hovered > at ? 'right' : 'active';

  const go = useCallback(
    (delta: number) => onSelect(Math.min(Math.max(0, at + delta), Math.max(0, count - 1))),
    [at, count, onSelect],
  );

  /* A carousel drag begins only on the central case: the hit layer's active box is the only
   * pointer target that calls this. The hover goes off for the duration, so the cases do not
   * lift and turn under the hand that is moving the row. */
  const beginDrag = (event: ReactPointerEvent) => {
    if (event.button !== 0) return;
    setHovered(null);
    /* No pointer capture: it would retarget the click to the shelf and the case would never
     * open its tracks. The window's pointerup still ends a release that lands outside. */
    const root = parseFloat(getComputedStyle(document.documentElement).fontSize) || 16;
    dragRem.current = root * fit;
    dragStart.current = event.clientX;
    dragStartAt.current = at;
    dragTarget.current = at;
    dragged.current = false;
  };

  /* The arrows walk the shelf from anywhere in the Media view, not only while it holds the
   * focus; a text field keeps its own arrows, and the pointer no longer drags the row. */
  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.metaKey || event.ctrlKey || event.altKey) return;
      if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
      const target = event.target as HTMLElement | null;
      if (target?.closest('input, textarea, select, [contenteditable="true"]')) return;
      event.preventDefault();
      go(event.key === 'ArrowLeft' ? -1 : 1);
    };
    document.addEventListener('keydown', onKeyDown);
    return () => document.removeEventListener('keydown', onKeyDown);
  }, [go]);

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
          data-hover-side={hoverSide}
          role="group"
          aria-roledescription="album shelf"
          tabIndex={0}
          onDragStart={(event) => event.preventDefault()}
          onPointerMove={(event) => {
            if (dragStart.current === null) return;
            if ((event.buttons & 1) === 0) {
              endRef.current(event.clientX);
              return;
            }
            const dx = event.clientX - dragStart.current;
            if (Math.abs(dx) > 10) dragged.current = true;
            const target = Math.min(
              Math.max(0, dragStartAt.current + stepsFor(dx)),
              Math.max(0, count - 1),
            );
            if (target === dragTarget.current) return;
            dragTarget.current = target;
            onSelect(target);
          }}
          onPointerUp={(event) => endRef.current(event.clientX)}
          onPointerCancel={() => endRef.current(null)}
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
                  data-active={activeCase}
                  data-hover={hovered === index}
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
                    <span className="k7-face k7-back" aria-hidden="true">
                      <span className="k7-sheen" aria-hidden="true" />
                    </span>
                    <span className="k7-face k7-side k7-right" aria-hidden="true">
                      <span className="k7-sheen" aria-hidden="true" />
                    </span>
                    <span className="k7-face k7-side k7-left" aria-hidden="true">
                      <span className="k7-spine">
                        <span className="k7-spine-group">{byline(album)}</span> -{' '}
                        {album.album}
                      </span>
                      <span className="k7-frame" />
                      <span className="k7-sheen" aria-hidden="true" />
                    </span>
                  </button>
                </div>
              );
            })}
          </div>

          {/* The pointer's targets, at the cases' rest places and outside the row's hover
              transform. The row and the cases animate above them; the pointer never has to
              follow a moving 3D box. */}
          <div className="k7-shelf-hits" aria-hidden="true">
            {albums.map((album, index) => {
              const distance = index - at;
              if (Math.abs(distance) > WINDOW) return null;
              return (
                <span
                  key={album.album}
                  className="k7-shelf-hit"
                  data-active={distance === 0}
                  style={{ transform: placeAt(distance) }}
                  onPointerDown={distance === 0 ? beginDrag : undefined}
                  onPointerEnter={() => {
                    if (dragStart.current !== null) return;
                    setHovered(index);
                  }}
                  onPointerLeave={() =>
                    setHovered((current) => (current === index ? null : current))
                  }
                  onClick={() => {
                    if (dragged.current) {
                      dragged.current = false;
                      return;
                    }
                    onSelect(index);
                    onOpen?.();
                  }}
                />
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
export interface IndexGroup {
  key: string;
  label: string;
  first: number;
  count: number;
}

/**
 * The stage's index rail: the artists' initials (the letters) or the genres, one plaque per
 * group, at the stage's full height. The buttons that do not fit are reached by the arrows,
 * and the list hides its bar. A name rail is wider, because a genre is not one character.
 */
export function IndexRail({
  groups,
  current,
  variant,
  onJump,
}: {
  groups: IndexGroup[];
  current: string | null;
  variant: 'letter' | 'name';
  onJump: (group: IndexGroup) => void;
}) {
  const buttons = useRef(new Map<string, HTMLButtonElement>());
  const list = useRef<HTMLDivElement>(null);
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
  }, [groups.length]);

  const scroll = (direction: number) => list.current?.scrollBy({ top: direction * 108 });

  if (groups.length === 0) return null;
  const stop =
    current !== null && groups.some((group) => group.key === current)
      ? current
      : groups[0].key;

  const jump = (group: IndexGroup) => onJump(group);
  const step = (key: string, delta: number) => {
    const at = groups.findIndex((group) => group.key === key);
    const next = groups[Math.min(Math.max(0, at + delta), groups.length - 1)];
    if (next === undefined) return;
    jump(next);
    buttons.current.get(next.key)?.focus();
  };

  return (
    <nav
      aria-label={variant === 'letter' ? 'Artists by letter' : 'Albums by genre'}
      className={`flex h-full shrink-0 flex-col items-center gap-1 ${
        variant === 'letter' ? 'w-8' : 'w-28'
      }`}
    >
      <Button
        type="button"
        size="icon-sm"
        variant="outline"
        aria-label="Scroll the index up"
        disabled={atTop}
        onClick={() => scroll(-1)}
        className={overflow ? '' : 'invisible'}
      >
        <ChevronUp aria-hidden="true" />
      </Button>
      <div
        ref={list}
        className="flex min-h-0 flex-1 flex-col items-center gap-1 overflow-y-auto [scrollbar-width:none] [&::-webkit-scrollbar]:hidden w-28"
      >
        {groups.map((group) => {
          const active = group.key === current;
          return (
            <button
              key={group.key}
              type="button"
              ref={(node) => {
                if (node) buttons.current.set(group.key, node);
                else buttons.current.delete(group.key);
              }}
              tabIndex={group.key === stop ? 0 : -1}
              aria-current={active ? 'true' : undefined}
              aria-label={`${group.label} — ${plural(group.count, 'album')}`}
              className={`flex shrink-0 items-center rounded-[6px] border border-foreground/45 leading-none outline-none transition-colors select-none focus-visible:ring-2 focus-visible:ring-ring ${
                variant === 'letter'
                  ? 'size-8 justify-center p-0'
                  : 'h-8 w-full justify-start px-2'
              } ${
                variant === 'name' && active
                  ? 'bg-secondary text-secondary-foreground'
                  : 'hover:bg-foreground/10'
              }`}
              onClick={() => jump(group)}
              onKeyDown={(event) => {
                if (event.key === 'ArrowDown') {
                  event.preventDefault();
                  step(group.key, 1);
                } else if (event.key === 'ArrowUp') {
                  event.preventDefault();
                  step(group.key, -1);
                } else if (event.key === 'Home') {
                  event.preventDefault();
                  step(group.key, -groups.length);
                } else if (event.key === 'End') {
                  event.preventDefault();
                  step(group.key, groups.length);
                }
              }}
            >
              {variant === 'letter' ? (
                <span
                  className={`flex size-5 items-center justify-center rounded-full border border-foreground/45 text-xs leading-none ${
                    active ? 'bg-secondary font-medium text-secondary-foreground' : 'text-foreground'
                  }`}
                >
                  {group.label}
                </span>
              ) : (
                <span className="truncate text-xs leading-none">{group.label}</span>
              )}
            </button>
          );
        })}
      </div>
      <Button
        type="button"
        size="icon-sm"
        variant="outline"
        aria-label="Scroll the index down"
        disabled={atEnd}
        onClick={() => scroll(1)}
        className={overflow ? '' : 'invisible'}
      >
        <ChevronDown aria-hidden="true" />
      </Button>
    </nav>
  );
}

/** The album's face: its own art, or the drawn cover it gets when it has no picture. */
function CaseCover({ album }: { album: Album }) {
  return (
    <CassetteFront
      cover={album.cover}
      initial={initialOf(album)}
      art={{ title: album.album, artist: byline(album) }}
    />
  );
}
