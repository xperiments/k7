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
  const built = buildJCard(ed.jcard);
  const design = {
    id: ed.tpl.id,
    name: ed.tpl.name,
    inks: ed.tpl.inks,
    jcard: {
      space: built.doc.space,
      paper: built.doc.paper,
      fonts: built.doc.fonts,
      prims: built.doc.prims,
    } as unknown as JcardSection,
  } as unknown as JcardDesign;
  const fields = {
    title: ed.jcard.sims?.title ?? "",
    artist: ed.jcard.sims?.artist ?? "",
    meta: ed.jcard.sims?.meta ?? "",
  } as unknown as JcardFields;

  return (
    <div className="k7app-preview flex h-full min-h-[480px] w-full flex-col justify-center overflow-hidden bg-k7-well px-6 pt-8">
      <JcardShelf design={design} fields={fields} />
    </div>
  );
}
