import { useCallback, useRef, type CSSProperties } from 'react';
import { ChevronLeft, ChevronRight } from 'lucide-react';

import { Button } from '@/components/ui/button';
import type { Album } from '../media/albums';
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

/** rem, and the same numbers as `--cw`, `--cd`, `--cg` in `index.css`. */
const CASE_W = 13;
const CASE_D = 2.6;
const CASE_GAP = 0.65;
/** Extra air on either side of the turned case, so it is plainly the one selected. */
const SELECTED_GAP = 1.25;
/** How far either side of the selection the row is built; the rest is off the shelf. */
const WINDOW = 10;

function byline(album: Album): string {
  return album.artists.length === 1 ? album.artists[0] : plural(album.artists.length, 'artist');
}

function initialOf(album: Album): string {
  return (album.album.trim().slice(0, 1) || '?').toUpperCase();
}

/** Where a case at distance `d` from the selection sits, in rem. */
function placeAt(distance: number): string {
  if (distance === 0) return 'translate3d(0, 0, 0)';
  const steps = Math.abs(distance) - 1;
  const x = CASE_W / 2 + CASE_GAP + SELECTED_GAP + steps * (CASE_D + CASE_GAP) + CASE_D / 2;
  const z = -Math.min(Math.abs(distance), 8) * 1.56;
  return `translate3d(${(distance > 0 ? x : -x).toFixed(3)}rem, 0, ${z.toFixed(2)}rem)`;
}

export function AlbumShelf({
  albums,
  selected,
  onSelect,
}: {
  albums: Album[];
  /** Which case is turned; the page owns it because the tracks below belong to it. */
  selected: number;
  onSelect: (index: number) => void;
}) {
  const dragFrom = useRef<number | null>(null);
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
    <section className="flex flex-col items-center gap-3" aria-label="Albums">
      <div
        className="k7-shelf"
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

/** The album's face: its own art, or a tinted face with its initial. */
function CaseCover({ album }: { album: Album }) {
  return <CassetteFront cover={album.cover} initial={initialOf(album)} />;
}
