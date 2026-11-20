import { useEditor } from "@/hooks/use-editor";

/**
 * The J-card's preview: the cassette its cover wraps, in CSS 3D. The sleeve's cover is the
 * front face, its spine the left one and its flap the back; the shell, the two reels and
 * the window are the appliance's. The cover's paper and the three fields are the design's.
 */
export function JCardPreview() {
  const ed = useEditor();
  const jc = ed.jcard;
  const paper = jc.paper ?? ed.tpl.inks.paper;
  const ink = ed.tpl.inks.ink;
  const rule = ed.tpl.inks.rule;
  const field = (name: string, fallback: string) => jc.sims?.[name] || fallback;

  const reel = (side: "A" | "B") => (
    <div
      key={side}
      className="absolute top-1/2 flex -translate-y-1/2 items-center justify-center rounded-full"
      style={{
        [side === "A" ? "left" : "right"]: "8%",
        width: "34%",
        aspectRatio: "1",
        background: "radial-gradient(circle, #26282d 0 46%, #3b3e45 47% 58%, #1b1c20 59%)",
        border: "2px solid rgba(255,255,255,0.12)",
      }}
    >
      <div
        className="rounded-full"
        style={{
          width: "58%",
          aspectRatio: "1",
          background: `radial-gradient(circle, ${paper} 0 28%, #191a1e 29% 54%, ${paper} 55%)`,
          boxShadow: "inset 0 0 0 2px rgba(0,0,0,0.55)",
        }}
      />
      <span
        className="absolute bottom-[6%] text-[10px] font-semibold tracking-[0.2em]"
        style={{ color: "rgba(255,255,255,0.55)" }}
      >
        {side}
      </span>
    </div>
  );

  return (
    <div className="flex h-full min-h-[480px] w-full items-center justify-center overflow-hidden bg-[var(--k7-bg)]">
      <div style={{ perspective: 1500 }}>
        <div
          className="relative"
          style={{
            width: 620,
            height: 400,
            transformStyle: "preserve-3d",
            transform: "rotateX(-13deg) rotateY(-25deg) rotateZ(1deg)",
          }}
        >
          {/* The shell's depth, behind every face. */}
          <div
            className="absolute rounded-[26px]"
            style={{
              inset: 0,
              transform: "translateZ(-46px)",
              background: "linear-gradient(145deg, #2c2f35, #121317)",
              boxShadow: "0 70px 90px -36px rgba(0,0,0,0.85)",
            }}
          />

          {/* The sleeve's cover: the sheet's paper and its three fields. */}
          <div
            className="absolute overflow-hidden rounded-[18px]"
            style={{
              inset: 0,
              background: paper,
              border: `1px solid ${rule}`,
              transform: "translateZ(2px)",
            }}
          >
            <div
              className="flex h-full w-full flex-col justify-between px-[7%] py-[6%]"
              style={{ color: ink }}
            >
              <div className="flex items-center justify-between text-[11px] font-medium tracking-[0.28em] opacity-80">
                <span>K7</span>
                <span>C-60</span>
              </div>
              <div className="flex flex-col gap-1">
                <span className="truncate text-[44px] font-semibold leading-none tracking-tight">
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
            {reel("A")}
            {reel("B")}
          </div>

          {/* The spine: the left face, printed with the same paper and the title. */}
          <div
            className="absolute"
            style={{
              left: 0,
              top: 0,
              width: 46,
              height: "100%",
              transformOrigin: "left center",
              transform: "rotateY(-90deg)",
              background: paper,
              borderLeft: `1px solid ${rule}`,
              borderTop: `1px solid ${rule}`,
            }}
          >
            <div
              className="flex h-full items-center justify-center"
              style={{ color: ink, transform: "rotate(180deg)" }}
            >
              <span className="truncate text-[13px] font-medium tracking-[0.18em]">
                {field("title", "Untitled")}
              </span>
            </div>
          </div>

          {/* The top edge: the flap folds over the shell here. */}
          <div
            className="absolute"
            style={{
              left: 0,
              top: 0,
              width: "100%",
              height: 46,
              transformOrigin: "center top",
              transform: "rotateX(90deg)",
              background: paper,
              borderTop: `1px solid ${rule}`,
            }}
          />

          {/* The back: the flap's face, in shadow. */}
          <div
            className="absolute rounded-[18px]"
            style={{
              inset: 0,
              transform: "translateZ(-44px) rotateY(180deg)",
              background: `linear-gradient(145deg, ${paper}, color-mix(in srgb, ${paper} 55%, #000))`,
              border: `1px solid ${rule}`,
            }}
          />
        </div>
      </div>
    </div>
  );
}
