import type { CSSProperties } from 'react';
const cn = (...c: Array<string | false | null | undefined>) => c.filter(Boolean).join(' ');

import type { JcardDesign, JcardFields, JcardPart } from './model';
import { jcardFrame, type JcardFit } from './render';

/**
 * A J-card drawn in the DOM, decorative and clipped to one panel. The sheet's sizes are
 * shares of the surface's width, so one design works at the shelf's 13rem case, in a square
 * card and in the Player bar's 30px case; the stylesheet hides its words below 6rem
 * (k7-look.md K19, k7art.md §5).
 *
 * It is `aria-hidden`: the album or Track it belongs to is named beside it.
 */
export function Jcard({
  design,
  fields,
  part = 'cover',
  fit = 'fill',
  className,
  style,
}: {
  design: JcardDesign;
  fields: JcardFields;
  part?: JcardPart;
  fit?: JcardFit;
  className?: string;
  style?: CSSProperties;
}) {
  const frame = jcardFrame(design, fields, part, fit);
  return (
    <span
      aria-hidden="true"
      className={cn('k7-jcard', className)}
      data-jcard={design.id}
      data-part={part}
      style={style}
    >
      <span className="k7-jcard-sheet" style={frame.sheet}>
        {frame.nodes.map((node) =>
          node.kind === 'rect' ? (
            <span key={node.key} className="k7-jcard-rect" style={node.style} />
          ) : (
            <span key={node.key} className="k7-jcard-text" style={node.style}>
              {node.content}
            </span>
          ),
        )}
      </span>
    </span>
  );
}
