import { useEditor } from "@/hooks/use-editor";
import { buildJCard } from "@/engine/jcard";
import { JcardShelf } from "../preview/shelf/shelf";
import type { JcardDesign, JcardFields, JcardSection } from "../preview/jcard/model";
import "../preview/preview.css";

/**
 * The J-card preview: the app's own shelf, drawing this design with the app's own J-card
 * renderer, scoped so its stylesheet cannot touch the editor. Two cases, as on the media
 * page: the turned one shows the cover part, the one beside it, spine out, the spine part.
 */
export function JCardPreview() {
  const ed = useEditor();
  const built = buildJCard(ed.jcard, ed.tpl);
  const design = {
    id: ed.tpl.id,
    name: ed.tpl.name,
    jcard: {
      space: built.doc.space,
      background: built.doc.background,
      fonts: built.doc.fonts,
      prims: built.doc.prims,
    } as unknown as JcardSection,
  } as unknown as JcardDesign;
  /* What an album's case fills, which is what the shelf draws: its name and byline, the
   * cassette line and its genre. A design bound to a Track's field — a tempo, a key — prints
   * nothing here, as it would on the shelf: the design's own test value is the canvas's. */
  const fields: JcardFields = {
    title: ed.jcardSim("title"),
    artist: ed.jcardSim("artist"),
    meta: ed.jcardSim("meta"),
    song_title: ed.jcardSim("song_title"),
    song_artist: ed.jcardSim("song_artist"),
    song_album: ed.jcardSim("song_album"),
    genre: ed.jcardSim("genre"),
  };

  return (
    <div className="k7app-preview flex h-full min-h-[480px] w-full flex-col justify-center overflow-hidden bg-k7-well px-6 pt-8">
      <JcardShelf design={design} fields={fields} />
    </div>
  );
}
