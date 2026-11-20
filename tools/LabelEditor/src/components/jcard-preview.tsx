import { useEditor } from "@/hooks/use-editor";

/**
 * The J-card's preview: the same case the albums draw (k7-case) — the cover on the front
 * face with the frame and the sheen, the title on the spine, and the slow turn that shows
 * both. The paper and the three fields come from the design; the case is the appliance's.
 */
export function JCardPreview() {
  const ed = useEditor();
  const jc = ed.jcard;
  const paper = jc.paper ?? ed.tpl.inks.paper;
  const ink = ed.tpl.inks.ink;
  const field = (name: string, fallback: string) => jc.sims?.[name] || fallback;

  return (
    <div className="flex h-full min-h-[480px] w-full items-center justify-center overflow-hidden bg-k7-well">
      <div className="k7-case-stage">
        <div className="k7-case-preview">
          {/* The cover: the sheet's paper, its three fields, the case's frame and sheen. */}
          <div className="k7-face k7-front" style={{ background: paper, color: ink }}>
            <div className="k7-frame" />
            <div className="k7-sheen" />
            <div className="relative flex h-full w-full flex-col justify-between px-[7%] py-[6%]">
              <div className="flex items-center justify-between text-[11px] font-medium tracking-[0.28em] opacity-80">
                <span>K7</span>
                <span>C-60</span>
              </div>
              <div className="flex min-w-0 flex-col gap-1">
                <span className="truncate text-[42px] font-semibold leading-none tracking-tight">
                  {field("title", "Untitled")}
                </span>
                <span className="truncate text-[18px] leading-tight opacity-90">
                  {field("artist", "Unknown artist")}
                </span>
                <span className="truncate text-[12px] tracking-[0.14em] opacity-70">
                  {field("meta", "Side A · Side B")}
                </span>
              </div>
            </div>
          </div>
          {/* The spine: the left face, printed with the same paper and the title. */}
          <div className="k7-face k7-left" style={{ background: paper, color: ink }}>
            <span className="k7-spine-title">{field("title", "Untitled")}</span>
          </div>
        </div>
      </div>
    </div>
  );
}
