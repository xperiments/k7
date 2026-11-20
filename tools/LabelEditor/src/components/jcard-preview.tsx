import { useEditor } from "@/hooks/use-editor";
import { buildJCard } from "@/engine/jcard";
import { Jcard } from "../preview/jcard/face";
import type { JcardDesign, JcardFields, JcardSection } from "../preview/jcard/model";
import "../preview/preview.css";

/**
 * The J-card preview: the app's own case and J-card renderer, fed with this design and
 * scoped so its stylesheet cannot touch the editor. One case faces the camera with its
 * spine and one with its cover — the two views the shelf uses.
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

  const Case = ({ facing }: { facing: "spine" | "cover" }) => (
    <div className="k7-case-shelf">
      <div className="k7-case" data-active={facing === "cover"}>
        <span className="k7-face k7-front">
          <Jcard design={design} fields={fields} part="cover" fit="fill" />
        </span>
        <span className="k7-face k7-left">
          <Jcard design={design} fields={fields} part="spine" fit="fill" />
        </span>
      </div>
    </div>
  );

  return (
    <div className="k7app-preview flex h-full min-h-[480px] w-full items-center justify-center gap-16 overflow-hidden bg-k7-well">
      <Case facing="spine" />
      <Case facing="cover" />
    </div>
  );
}
