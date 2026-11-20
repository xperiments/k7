import { useEditor } from "@/hooks/use-editor";
import { JCARD_H, JCARD_PARTS } from "@/engine/jcard";
import type { JCardPart } from "@/engine/jcard";
import { alignOf } from "@/engine/tpl";

/**
 * The J-card in the two views the shelf uses: one case with its spine toward the camera and
 * one with its cover. Both carry the sheet's own parts — the cover part on the front, the
 * spine part on the left — so the preview and the shelf show the same thing.
 */
export function JCardPreview() {
  const ed = useEditor();
  const jc = ed.jcard;
  const paper = jc.paper ?? ed.tpl.inks.paper;

  const frontW = 360;
  const frontH = Math.round((frontW * JCARD_H) / JCARD_PARTS.cover.w);
  const spineW = Math.round((JCARD_PARTS.spine.w / JCARD_PARTS.cover.w) * frontW);

  const part = (p: JCardPart) => {
    const box = JCARD_PARTS[p];
    const s = (p === "spine" ? spineW : frontW) / box.w;
    return jc.prims
      .filter((o) => o.x >= box.x && o.x < box.x + box.w)
      .map((o) => {
        const left = (o.x - box.x) * s;
        const top = o.y * s;
        const w = o.w * s;
        const h = (o.h || 0) * s;
        const colour = ed.roleColor(o.role ?? "") ?? "transparent";
        return o.type === "text" ? (
          <div
            key={o.id}
            style={{
              position: "absolute",
              left,
              top,
              width: w,
              color: colour,
              fontSize: ed.pxOf(o) * s,
              fontFamily: `"${ed.ffOf(o)}", sans-serif`,
              textAlign: alignOf(o),
              lineHeight: 1.2,
              whiteSpace: "pre-wrap",
              overflow: "hidden",
              transform: o.rot ? `rotate(${o.rot}deg)` : undefined,
              transformOrigin: "50% 50%",
            }}
          >
            {ed.simText(o)}
          </div>
        ) : (
          <div
            key={o.id}
            style={{
              position: "absolute",
              left,
              top,
              width: w,
              height: h,
              background: colour,
              borderRadius: o.type === "circle" ? "50%" : (o.r ?? 0) * s,
              transform: o.rot ? `rotate(${o.rot}deg)` : undefined,
              transformOrigin: "50% 50%",
            }}
          />
        );
      });
  };

  const Case = ({ facing }: { facing: "spine" | "cover" }) => (
    <div className="k7-case-stage">
      <div className={`k7-case-preview is-${facing}`} style={{ width: frontW, height: frontH }}>
        <div className="k7-face k7-front" style={{ background: paper }}>
          <div className="k7-frame" />
          <div className="k7-sheen" />
          <div style={{ position: "absolute", inset: 0 }}>{part("cover")}</div>
        </div>
        <div
          className="k7-face k7-left"
          style={{
            background: paper,
            width: spineW,
            transform: `rotateY(-90deg) translateZ(${spineW / 2}px)`,
          }}
        >
          <div style={{ position: "absolute", inset: 0 }}>{part("spine")}</div>
        </div>
      </div>
    </div>
  );

  return (
    <div className="flex h-full min-h-[480px] w-full items-center justify-center gap-16 overflow-hidden bg-k7-well">
      <Case facing="spine" />
      <Case facing="cover" />
    </div>
  );
}
