/**
 * The rail's Albums glyph: a frame over three thumbnails, drawn here rather than
 * imported because the owner's glyph has no Lucide twin (k7-look.md K23 names it as
 * the one hand-drawn icon).
 *
 * It is the supplied `gallery-thumbnails` outline verbatim: the walls are 1 unit of
 * `currentColor` drawn by fill rather than a 1.25 stroke, because at the rail's 16px
 * a stroke that thick closes the three thumbnails into one bar. The viewBox is inset
 * by 1.5 units so the glyph takes the same optical size as the Lucide icons beside it.
 */
export function GalleryGlyph({ className }: { className?: string }) {
  return (
    <svg
      viewBox="-1.5 -1.5 27 27"
      width={24}
      height={24}
      fill="currentColor"
      aria-hidden="true"
      className={className}
    >
      <path d="M20.5,15H3.5c-1.93,0-3.5-1.57-3.5-3.5V3.5C0,1.57,1.57,0,3.5,0H20.5c1.93,0,3.5,1.57,3.5,3.5V11.5c0,1.93-1.57,3.5-3.5,3.5ZM3.5,1c-1.38,0-2.5,1.12-2.5,2.5V11.5c0,1.38,1.12,2.5,2.5,2.5H20.5c1.38,0,2.5-1.12,2.5-2.5V3.5c0-1.38-1.12-2.5-2.5-2.5H3.5Zm.5,23H2c-1.1,0-2-.9-2-2v-2c0-1.1,.9-2,2-2h2c1.1,0,2,.9,2,2v2c0,1.1-.9,2-2,2Zm-2-5c-.55,0-1,.45-1,1v2c0,.55,.45,1,1,1h2c.55,0,1-.45,1-1v-2c0-.55-.45-1-1-1H2Zm20,5h-2c-1.1,0-2-.9-2-2v-2c0-1.1,.9-2,2-2h2c1.1,0,2,.9,2,2v2c0,1.1-.9,2-2,2Zm-2-5c-.55,0-1,.45-1,1v2c0,.55,.45,1,1,1h2c.55,0,1-.45,1-1v-2c0-.55-.45-1-1-1h-2Zm-7,5h-2c-1.1,0-2-.9-2-2v-2c0-1.1,.9-2,2-2h2c1.1,0,2,.9,2,2v2c0,1.1-.9,2-2,2Zm-2-5c-.55,0-1,.45-1,1v2c0,.55,.45,1,1,1h2c.55,0,1-.45,1-1v-2c0-.55-.45-1-1-1h-2Z" />
    </svg>
  );
}
