/**
 * The App's hand-drawn glyphs, and the only icons that are not Lucide (k7-look.md K23
 * names them): the rail's **Albums** frame over three thumbnails and its **Sources**
 * cassette tape. Both are the owners' outlines verbatim.
 *
 * They keep `currentColor` and the 24-unit grid, and differ from the icons beside them
 * in one way: their 1-unit walls are drawn by fill rather than a 1.25 stroke, because
 * at the rail's 16px a stroke that thick closes their small shapes into solid bars. The
 * viewBox is inset by 1.5 units so each takes the same optical size as its neighbours.
 */
const VIEW_BOX = '-1.5 -1.5 27 27';

function Glyph({ d, className }: { d: string; className?: string }) {
  return (
    <svg
      viewBox={VIEW_BOX}
      width={24}
      height={24}
      fill="currentColor"
      aria-hidden="true"
      className={className}
    >
      <path d={d} />
    </svg>
  );
}

/** Albums: a frame over three small thumbnails. */
export function GalleryGlyph({ className }: { className?: string }) {
  return (
    <Glyph
      className={className}
      d="M20.5,15H3.5c-1.93,0-3.5-1.57-3.5-3.5V3.5C0,1.57,1.57,0,3.5,0H20.5c1.93,0,3.5,1.57,3.5,3.5V11.5c0,1.93-1.57,3.5-3.5,3.5ZM3.5,1c-1.38,0-2.5,1.12-2.5,2.5V11.5c0,1.38,1.12,2.5,2.5,2.5H20.5c1.38,0,2.5-1.12,2.5-2.5V3.5c0-1.38-1.12-2.5-2.5-2.5H3.5Zm.5,23H2c-1.1,0-2-.9-2-2v-2c0-1.1,.9-2,2-2h2c1.1,0,2,.9,2,2v2c0,1.1-.9,2-2,2Zm-2-5c-.55,0-1,.45-1,1v2c0,.55,.45,1,1,1h2c.55,0,1-.45,1-1v-2c0-.55-.45-1-1-1H2Zm20,5h-2c-1.1,0-2-.9-2-2v-2c0-1.1,.9-2,2-2h2c1.1,0,2,.9,2,2v2c0,1.1-.9,2-2,2Zm-2-5c-.55,0-1,.45-1,1v2c0,.55,.45,1,1,1h2c.55,0,1-.45,1-1v-2c0-.55-.45-1-1-1h-2Zm-7,5h-2c-1.1,0-2-.9-2-2v-2c0-1.1,.9-2,2-2h2c1.1,0,2,.9,2,2v2c0,1.1-.9,2-2,2Zm-2-5c-.55,0-1,.45-1,1v2c0,.55,.45,1,1,1h2c.55,0,1-.45,1-1v-2c0-.55-.45-1-1-1h-2Z"
    />
  );
}

/** Sources: a cassette tape, the thing the card holds. */
export function CassetteGlyph({ className }: { className?: string }) {
  return (
    <Glyph
      className={className}
      d="M19.5,3H4.5C2.02,3,0,5.02,0,7.5v9c0,2.48,2.02,4.5,4.5,4.5h15c2.48,0,4.5-2.02,4.5-4.5V7.5c0-2.48-2.02-4.5-4.5-4.5ZM5.93,20l.89-1.33c.28-.42,.75-.67,1.25-.67h7.86c.5,0,.97,.25,1.25,.67l.89,1.33H5.93Zm17.07-3.5c0,1.93-1.57,3.5-3.5,3.5h-.23l-1.26-1.89c-.46-.7-1.24-1.11-2.08-1.11h-7.86c-.84,0-1.62,.42-2.08,1.11l-1.26,1.89h-.23c-1.93,0-3.5-1.57-3.5-3.5V7.5c0-1.93,1.57-3.5,3.5-3.5h15c1.93,0,3.5,1.57,3.5,3.5v9Zm-5.5-8.5H7c-1.65,0-3,1.35-3,3s1.35,3,3,3h10.5c1.65,0,3-1.35,3-3s-1.35-3-3-3Zm-2.23,1c-.48,.53-.77,1.23-.77,2s.29,1.47,.77,2h-6.03c.48-.53,.77-1.23,.77-2s-.29-1.47-.77-2h6.03Zm-10.27,2c0-1.1,.9-2,2-2s2,.9,2,2-.9,2-2,2-2-.9-2-2Zm12.5,2c-1.1,0-2-.9-2-2s.9-2,2-2,2,.9,2,2-.9,2-2,2Z"
    />
  );
}
