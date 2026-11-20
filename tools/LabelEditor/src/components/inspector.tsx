import { createContext, useContext, useState } from "react";
import {
  AlignCenter,
  AlignLeft,
  AlignRight,
  SlidersHorizontal,
  ChevronDown,
  FolderMinus,
  FolderPlus,
  Lock,
  LockOpen,
  MousePointerClick,
  PanelRightClose,
  PanelRightOpen,
  Plus,
  SquareDashed,
  Trash2,
} from "lucide-react";

import { Badge } from "@/components/reui/badge";
import { ColorPicker } from "@/components/reui/color-picker";
import {
  NumberField,
  NumberFieldDecrement,
  NumberFieldGroup,
  NumberFieldIncrement,
  NumberFieldInput,
} from "@/components/reui/number-field";
import { Button } from "@/components/ui/button";
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from "@/components/ui/empty";
import { Field, FieldLabel } from "@/components/ui/field";
import { Input } from "@/components/ui/input";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Switch } from "@/components/ui/switch";
import { ToggleGroup, ToggleGroupItem } from "@/components/ui/toggle-group";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import { useEditor } from "@/hooks/use-editor";
import {
  JCARD_FACES,
  JCARD_FIELDS,
  JCARD_FIELD_LABELS,
  JCARD_H,
  JCARD_PARTS,
  JCARD_SAFE,
  JCARD_W,
  type JCardField,
} from "@/engine/jcard";
import type { JCardFocus, Obj } from "@/engine/types";
import {
  CANVAS_MAX,
  FROLES,
  PAL,
  PALETTE,
  PRINT_H,
  PRINT_W,
  ROLES,
  SLOTS,
  TEXT_ROLES,
  WIN,
  slotOf,
} from "@/engine/vocab";

/** The inspector: the document when nothing is selected, the layer when one is. */
export function Inspector({ onCollapse }: { onCollapse: () => void }) {
  const ed = useEditor();
  const sel = ed.sel;
  const ids = ed.selIds;
  const active = ed.activeObjs();
  const selected = active.filter((o) => ids.includes(o.id));
  const many = selected.length > 1;
  const jcard = ed.isJCard();

  let title = jcard ? "J-card" : "Design";
  let subtitle = jcard ? "One sheet · flap | spine | cover" : "Colourway, type and fields";
  let body: React.ReactNode;

  if (ed.run) {
    title = "Test values";
    subtitle = "What each field prints in Preview";
    body = <PreviewFields />;
  } else if (ed.selMask && ed.mask) {
    title = "LayerMask";
    subtitle = "A rounded clip, the size of the print";
    body = <MaskPanel />;
  } else if (jcard && many) {
    title = `${selected.length} J-card layers`;
    subtitle = "Bulk actions and colour roles";
    body = <JCardMultiPanel key={[...ids].sort().join(",")} />;
  } else if (jcard && sel) {
    title = sel.type === "text" ? "J-card text" : sel.type === "circle" ? "J-card circle" : "J-card rectangle";
    subtitle = sel.name
      ? `field · ${sel.name}`
      : sel.label ||
        (sel.type === "text" && sel.static
          ? sel.text?.trim()
            ? sel.text.trim().slice(0, 32)
            : "empty text"
          : `layer ${sel.id}`);
    body = <JCardLayerPanel key={sel.id} o={sel} />;
  } else if (jcard) {
    body = <JCardDesignPanel />;
  } else if (many) {
    title = `${selected.length} layers`;
    subtitle = "Bulk actions and colour roles";
    body = <MultiPanel key={[...ids].sort().join(",")} />;
  } else if (sel) {
    title = sel.type === "text" ? "Text" : sel.type === "circle" ? "Circle" : "Rectangle";
    subtitle = sel.name
      ? `field · ${sel.name}`
      : sel.label ||
        (sel.type === "text" && sel.static
          ? sel.text?.trim()
            ? sel.text.trim().slice(0, 32)
            : "empty text"
          : `layer ${sel.id}`);
    body = <LayerPanel key={sel.id} o={sel} />;
  } else {
    body = <DesignPanel />;
  }

  return (
    <aside className="flex min-h-0 w-full flex-col border-l border-border bg-background" aria-label="Inspector">
      <div className="flex h-11 shrink-0 items-center gap-2 border-b border-border px-3">
        <SlidersHorizontal className="size-4 text-muted-foreground" />
        <div className="min-w-0">
          <h2 className="truncate text-xs font-medium tracking-tight text-foreground">{title}</h2>
          <p className="truncate text-xs text-muted-foreground">{subtitle}</p>
        </div>
        <div className="ml-auto flex items-center gap-1">
          {sel ? (
            <Tooltip>
              <TooltipTrigger
                render={
                  <Button
                    variant="ghost"
                    size="icon-sm"
                    aria-label={sel.locked ? "Unlock the layer" : "Lock the layer"}
                    onClick={() => ed.toggleLock(sel)}
                  >
                    {sel.locked ? <Lock /> : <LockOpen />}
                  </Button>
                }
              />
              <TooltipContent>{sel.locked ? "Unlock" : "Lock"}</TooltipContent>
            </Tooltip>
          ) : null}
          <Tooltip>
            <TooltipTrigger
              render={
                <Button
                  variant="ghost"
                  size="icon-sm"
                  aria-label="Collapse the inspector"
                  onClick={onCollapse}
                >
                  <PanelRightClose />
                </Button>
              }
            />
            <TooltipContent>Collapse the inspector</TooltipContent>
          </Tooltip>
        </div>
      </div>
      <div className="min-h-0 flex-1 overflow-y-auto">{body}</div>
    </aside>
  );
}

/** The inspector, collapsed: a strip that brings it back. */
export function InspectorRail({ onExpand }: { onExpand: () => void }) {
  return (
    <aside
      className="hidden w-11 shrink-0 flex-col items-center gap-2 border-l border-border bg-background py-2 lg:flex"
      aria-label="Inspector, collapsed"
    >
      <Tooltip>
        <TooltipTrigger
          render={
            <Button
              variant="ghost"
              size="icon-sm"
              aria-label="Expand the inspector"
              onClick={onExpand}
            >
              <PanelRightOpen />
            </Button>
          }
        />
        <TooltipContent>Expand the inspector</TooltipContent>
      </Tooltip>
      <span
        className="text-xs uppercase tracking-[0.14em] text-muted-foreground"
        style={{ writingMode: "vertical-rl" }}
      >
        Inspector
      </span>
    </aside>
  );
}

// ---- the design: what the document is, when no layer is selected ----

function DesignPanel() {
  const ed = useEditor();
  return (
    <SectionGroup initial="Template">
      <Section title="Template">
        <Field className="gap-1.5">
          <FieldLabel className="text-xs text-muted-foreground">ID</FieldLabel>
          <Input
            value={ed.tpl.id}
            onChange={(e) => ed.setTplField("id", e.target.value)}
            className="h-8 font-mono text-xs"
            spellCheck={false}
            aria-label="Template id"
          />
        </Field>
        <Field className="gap-1.5">
          <FieldLabel className="text-xs text-muted-foreground">Name</FieldLabel>
          <Input
            value={ed.tpl.name}
            onChange={(e) => ed.setTplField("name", e.target.value)}
            className="h-8"
            aria-label="Template name"
          />
        </Field>
        <div className="flex items-center justify-between rounded-md border border-border px-2 py-1.5">
          <span className="text-xs text-muted-foreground">Print</span>
          <span className="text-xs tabnum">
            {PRINT_W} × {PRINT_H}
          </span>
        </div>
      </Section>

      <Section title="Colors">
        <p className="text-xs text-muted-foreground">
          A layer names a role; every layer with that role follows its colour.
        </p>
        <div className="grid grid-cols-2 gap-2">
          <ColourCell label="Ink" value={ed.tpl.inks.ink} onChange={(v) => ed.setRoleColor("ink", v)} />
          <ColourCell label="Rule" value={ed.tpl.inks.rule} onChange={(v) => ed.setRoleColor("rule", v)} />
          {ed.tpl.inks.band.map((c, i) => (
            <ColourCell
              key={i}
              label={`Band ${i}`}
              value={c}
              onChange={(v) => ed.setRoleColor(`band.${i}`, v)}
            />
          ))}
        </div>
        <p className="text-xs text-muted-foreground">
          {ed.objs.filter((o) => o.role && o.role.startsWith("band.")).length} layers use a band
          · {ed.objs.filter((o) => o.role === "ink").length} use the ink
        </p>
      </Section>

      <Section
        title="Paper"
        action={
          ed.mask ? (
            <Button variant="ghost" size="icon-xs" onClick={() => ed.removeMask()} aria-label="Remove the LayerMask">
              <Trash2 />
            </Button>
          ) : undefined
        }
      >
        {ed.mask ? (
          <>
            <ColourRow label="Paper" value={ed.mask.color} onChange={(v) => ed.setMaskField("color", v)} />
            <Num label="Corner radius" value={ed.mask.radius} min={0} onChange={(v) => ed.setMaskField("radius", v)} />
            <p className="text-xs text-muted-foreground">
              The paper clips every layer inside the LayerMask to a rounded rectangle the
              size of the print.
            </p>
          </>
        ) : (
          <Empty className="border p-3">
            <EmptyHeader>
              <EmptyMedia variant="icon">
                <SquareDashed />
              </EmptyMedia>
              <EmptyTitle>No paper</EmptyTitle>
              <EmptyDescription>
                A LayerMask adds the printed paper, its role colour and the mask that clips
                the design.
              </EmptyDescription>
            </EmptyHeader>
            <Button size="sm" variant="outline" onClick={() => ed.addMask()}>
              Add LayerMask
            </Button>
          </Empty>
        )}
      </Section>

      {/* <Section title="Type roles">
        {FROLES.map((k) => {
          const cur = ed.tpl.fonts[k];
          return (
            <div key={k} className="flex flex-col gap-1.5 rounded-md border border-border p-2">
              <span className="text-xs uppercase tracking-[0.08em] text-muted-foreground">
                {k}
              </span>
              <Select
                value={cur.ff}
                onValueChange={(v) => v && ed.setTplField(`ff:${k}`, v)}
                items={sheetFaces.map((f) => ({ value: f, label: f }))}
              >
                <SelectTrigger size="sm" className="w-full" aria-label={`${k} face`}>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent alignItemWithTrigger={false}>
                  {sheetFaces.map((f) => (
                    <SelectItem key={f} value={f}>
                      {f}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
              <Select
                value={String(cur.px)}
                onValueChange={(v) => v && ed.setTplField(`px:${k}`, v)}
                items={sheetSizes(cur.ff).map((n) => ({ value: String(n), label: `${n} px` }))}
              >
                <SelectTrigger size="sm" className="w-full" aria-label={`${k} size`}>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent alignItemWithTrigger={false}>
                  {sheetSizes(cur.ff).map((n) => (
                    <SelectItem key={n} value={String(n)}>
                      {n} px
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
          );
        })}
        <p className="text-xs text-muted-foreground">
          Only the pairs the sheet compiles are offered; a design can only print them.
        </p>
      </Section> */}

      <Section title="Canvas">
        <ColourRow label="Active background" value={ed.bg} onChange={(v) => ed.setBg(v)} />
        <div className="flex flex-col gap-1.5">
          <div className="flex items-center justify-between">
            <span className="text-xs text-muted-foreground">Backgrounds</span>
            <span className="text-xs text-muted-foreground tabnum">
              {ed.bgi + 1} / {ed.bgs.length}
            </span>
          </div>
          <div
            className="flex flex-wrap items-center gap-1.5"
            role="group"
            aria-label="Canvas backgrounds"
          >
            {ed.bgs.map((c, i) => (
              <button
                key={i}
                type="button"
                title={`Background ${i + 1} · ${c}`}
                aria-label={`Background ${i + 1}`}
                aria-pressed={i === ed.bgi}
                onClick={() => ed.selectBg(i)}
                className={`size-6 rounded-full border transition-colors ${
                  i === ed.bgi
                    ? "border-primary ring-2 ring-primary/40"
                    : "border-border hover:border-foreground/50"
                }`}
                style={{ background: c }}
              />
            ))}
            <Button
              variant="outline"
              size="sm"
              onClick={() => ed.addBg()}
              disabled={ed.bgs.length >= CANVAS_MAX}
            >
              <Plus />
              Add
            </Button>
            <Button
              variant="ghost"
              size="icon-sm"
              onClick={() => ed.removeBg()}
              disabled={ed.bgs.length <= 1}
              aria-label="Remove the active background"
            >
              <Trash2 />
            </Button>
          </div>
        </div>
        <div className="flex items-center justify-between rounded-md border border-border px-2 py-1.5">
          <span className="text-xs text-muted-foreground">Screen</span>
          <span className="text-xs tabnum">800 × 480</span>
        </div>
        <p className="text-xs text-muted-foreground">
          The cassette's background while the two-finger pose is up: its body and the screen
          behind it. The K7 picks one by the Track's cassette row (the list cycles); the
          active is your preview. Add copies it.
        </p>
      </Section>

      <Section title="Test values">
        <p className="text-xs text-muted-foreground">
          What each field prints until the resolver sends a real value.
        </p>
        <div className="grid grid-cols-2 gap-2">
          {SLOTS.map((s) => (
            <Field key={s.id} className="gap-1">
              <FieldLabel className="text-xs text-muted-foreground">{s.label}</FieldLabel>
              <Input
                value={ed.simOf(s.id)}
                onChange={(e) => ed.setSim(s.id, e.target.value)}
                className="h-7 min-w-0 text-xs"
                aria-label={`Test value for ${s.label}`}
              />
            </Field>
          ))}
        </div>
      </Section>
    </SectionGroup>
  );
}

// ---- one layer ----

function LayerPanel({ o }: { o: Obj }) {
  const ed = useEditor();
  const locked = !!o.locked;
  const isText = o.type === "text";
  const dynamic = isText && !o.static;
  return (
    <SectionGroup initial={isText ? "Content" : "Layout"}>
      {isText ? (
        <Section title="Content">
          <>
            <Field className="gap-1.5">
              <FieldLabel className="text-xs text-muted-foreground">Kind</FieldLabel>
              <Select
                value={dynamic ? "dynamic" : "static"}
                onValueChange={(v) => {
                  if (v === "static") {
                    if (!o.text) o.text = ed.simText(o);
                    o.static = true;
                  } else {
                    delete o.static;
                  }
                  ed.commit(false);
                  ed.emit();
                }}
                items={[
                  { value: "dynamic", label: "Dynamic text (from a field)" },
                  { value: "static", label: "Static text" },
                ]}
              >
                <SelectTrigger size="sm" className="w-full" disabled={locked} aria-label="Text kind">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent alignItemWithTrigger={false}>
                  <SelectItem value="dynamic">Dynamic text (from a field)</SelectItem>
                  <SelectItem value="static">Static text</SelectItem>
                </SelectContent>
              </Select>
            </Field>
            {dynamic ? (
              <>
                <Field className="gap-1.5">
                  <FieldLabel className="text-xs text-muted-foreground">Field</FieldLabel>
                  <Select
                    value={o.name || ""}
                    onValueChange={(v) => {
                      if (!v) return;
                      o.name = v;
                      delete o.static;
                      ed.commit(false);
                      ed.emit();
                    }}
                    items={SLOTS.map((s) => ({ value: s.id, label: s.label }))}
                  >
                    <SelectTrigger size="sm" className="w-full" disabled={locked} aria-label="Field">
                      <SelectValue placeholder="Choose a field…" />
                    </SelectTrigger>
                    <SelectContent alignItemWithTrigger={false}>
                      {SLOTS.map((s) => (
                        <SelectItem key={s.id} value={s.id}>
                          {s.label}
                        </SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                  {!o.name ? (
                    <p className="text-xs text-muted-foreground">
                      Choose a field: a dynamic text with none prints nothing.
                    </p>
                  ) : null}
                </Field>
                <Field className="gap-1.5">
                  <FieldLabel className="text-xs text-muted-foreground">
                    Test value
                  </FieldLabel>
                  <Input
                    value={o.sim ?? ""}
                    placeholder={ed.simOf(o.name)}
                    onChange={(e) => {
                      if (e.target.value.trim() === "") delete o.sim;
                      else o.sim = e.target.value;
                      ed.commit(true);
                      ed.emit();
                    }}
                    disabled={locked}
                    className="h-7 text-xs"
                    aria-label="Test value"
                  />
                </Field>
              </>
            ) : (
              <Field className="gap-1.5">
                <FieldLabel className="text-xs text-muted-foreground">Text</FieldLabel>
                <Input
                  value={o.text ?? ""}
                  placeholder="What this text prints"
                  onChange={(e) => {
                    o.text = e.target.value;
                    ed.commit(false);
                    ed.emit();
                  }}
                  disabled={locked}
                  className="h-8"
                  aria-label="Text"
                />
                {!(o.text ?? "").trim() ? (
                  <p className="text-xs text-muted-foreground">
                    Empty prints nothing. Type the string here, or set Kind to a dynamic text
                    and choose a field.
                  </p>
                ) : null}
              </Field>
            )}
          </>
        </Section>
      ) : null}

      <Section title="Layout">
        <div className="grid grid-cols-2 gap-2">
          <Num label="X" value={o.x} onChange={(v) => ed.setObjField(o, "x", v)} disabled={locked} />
          <Num label="Y" value={o.y} onChange={(v) => ed.setObjField(o, "y", v)} disabled={locked} />
          <Num
            label={isText ? "Wrap width" : "Width"}
            value={o.w}
            min={1}
            onChange={(v) => ed.setObjField(o, "w", v)}
            disabled={locked}
          />
          {isText ? (
            <ReadOnly label="Height" value={`${Math.round(ed.pxOf(o) * 1.2)} auto`} />
          ) : (
            <Num
              label="Height"
              value={o.h ?? 0}
              min={1}
              onChange={(v) => ed.setObjField(o, "h", v)}
              disabled={locked}
            />
          )}
          {o.type === "rect" ? (
            <Num
              label="Radius"
              value={o.r ?? 0}
              min={0}
              onChange={(v) => ed.setObjField(o, "r", v)}
              disabled={locked}
            />
          ) : null}
          <Num
            label="Rotation °"
            value={Math.round(o.rot ?? 0)}
            onChange={(v) => ed.setObjField(o, "rot", v)}
            disabled={locked}
          />
        </div>
        {isText && o.static ? (
          <p className="text-xs text-muted-foreground">
            The box is fitted to the text's ink and moves with it.
          </p>
        ) : null}
      </Section>

      {isText ? (
        <Section title="Typography">
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Font role</FieldLabel>
            <Select
              value={o.fr ?? "caption"}
              onValueChange={(v) => ed.setObjField(o, "fr", v as Obj["fr"])}
              items={FROLES.map((f) => ({ value: f, label: f }))}
            >
              <SelectTrigger size="sm" className="w-full" disabled={locked} aria-label="Font role">
                <SelectValue />
              </SelectTrigger>
              <SelectContent alignItemWithTrigger={false}>
                {FROLES.map((f) => (
                  <SelectItem key={f} value={f}>
                    {f} · {ed.tpl.fonts[f].ff} {ed.tpl.fonts[f].px}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </Field>
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Align</FieldLabel>
            <ToggleGroup
              value={[o.align ?? "left"]}
              onValueChange={(v) => {
                if (v[0]) ed.setObjField(o, "align", v[0] as Obj["align"]);
              }}
              variant="outline"
              size="sm"
              spacing={0}
              aria-label="Text align"
              className="w-full"
            >
              <ToggleGroupItem value="left" className="flex-1" aria-label="Align left">
                <AlignLeft />
              </ToggleGroupItem>
              <ToggleGroupItem value="center" className="flex-1" aria-label="Align centre">
                <AlignCenter />
              </ToggleGroupItem>
              <ToggleGroupItem value="right" className="flex-1" aria-label="Align right">
                <AlignRight />
              </ToggleGroupItem>
            </ToggleGroup>
          </Field>
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Colour role</FieldLabel>
            <RoleSelect
              value={o.role ?? "ink"}
              roles={TEXT_ROLES}
              onChange={(v) => ed.setObjField(o, "role", v)}
              disabled={locked}
            />
          </Field>
        </Section>
      ) : (
        <Section title="Appearance">
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Colour role</FieldLabel>
            <RoleSelect
              value={o.role ?? "band.0"}
              roles={ROLES}
              onChange={(v) => ed.setObjField(o, "role", v)}
              disabled={locked}
            />
          </Field>
          {o.role ? (
            <>
              <ColourRow
                label={`Colour of "${o.role}"`}
                value={ed.roleColor(o.role) ?? "#000000"}
                onChange={(v) => ed.setRoleColor(o.role!, v)}
              />
              <SwatchStrip
                value={ed.roleColor(o.role) ?? ""}
                onPick={(c) => ed.setRoleColor(o.role!, c)}
              />
            </>
          ) : null}
          {o.type === "rect" ? (
            <label className="flex items-center gap-2 text-xs">
              <Switch
                checked={!!o.ap}
                onCheckedChange={(checked) => {
                  ed.objs.forEach((x) => {
                    if (x !== o) delete x.ap;
                  });
                  if (checked) o.ap = true;
                  else delete o.ap;
                  ed.commit(false);
                  ed.emit();
                }}
                disabled={locked}
              />
              Aperture <span className="text-muted-foreground">(the Card's window)</span>
            </label>
          ) : null}
        </Section>
      )}

      {isText ? (
        <Section title="Appearance">
          <ColourRow
            label={o.role ? `Colour of "${o.role}"` : "Colour"}
            value={o.role ? (ed.roleColor(o.role) ?? "#000000") : (o.tc ?? "#000000")}
            onChange={(v) => (o.role ? ed.setRoleColor(o.role, v) : ed.setObjField(o, "tc", v))}
          />
          {o.role ? (
            <SwatchStrip
              value={ed.roleColor(o.role) ?? ""}
              onPick={(c) => ed.setRoleColor(o.role!, c)}
            />
          ) : null}
        </Section>
      ) : null}

      <Section title="Layer">
        <Field className="gap-1.5">
          <FieldLabel className="text-xs text-muted-foreground">Name</FieldLabel>
          <Input
            value={o.label ?? ""}
            placeholder={`${o.type} ${o.id}`}
            onChange={(e) => {
              o.label = e.target.value;
              ed.commit(false);
              ed.emit();
            }}
            className="h-8"
            aria-label="Layer name"
          />
        </Field>
        <Field className="gap-1.5">
          <FieldLabel className="text-xs text-muted-foreground">
            Note <span className="opacity-60">(for a template's own colour)</span>
          </FieldLabel>
          <Input
            value={o.note ?? ""}
            onChange={(e) => {
              o.note = e.target.value;
              ed.commit(false);
              ed.emit();
            }}
            className="h-8"
            aria-label="Note"
          />
        </Field>
        <label className="flex items-center gap-2 text-xs">
          <Switch checked={locked} onCheckedChange={() => ed.toggleLock(o)} />
          Locked
          <span className="text-muted-foreground">· no edits, drags or deletes</span>
        </label>
        <Button variant="outline" size="sm" onClick={() => ed.selectMask()} disabled={!ed.mask}>
          <SquareDashed />
          Select the LayerMask
        </Button>
      </Section>
    </SectionGroup>
  );
}

// ---- the J-card: one sheet, its Type roles and its three Fields ----

const JCARD_FOCUS: Array<{ value: JCardFocus; label: string; detail: string }> = [
  { value: "sheet", label: "Sheet", detail: `${JCARD_W} × ${JCARD_H}` },
  { value: "flap", label: "Flap", detail: `${JCARD_PARTS.flap.w} × ${JCARD_H}` },
  { value: "spine", label: "Spine", detail: `${JCARD_PARTS.spine.w} × ${JCARD_H}` },
  { value: "cover", label: "Cover", detail: `${JCARD_PARTS.cover.w} × ${JCARD_H}` },
];

function JCardDesignPanel() {
  const ed = useEditor();
  return (
    <SectionGroup initial="Sheet">
      <Section title="Sheet">
        <ColourRow
          label="Paper (the sheet's colour)"
          value={ed.tpl.inks.paper}
          onChange={(v) => ed.setRoleColor("paper", v)}
        />
        <div className="grid grid-cols-2 gap-2">
          <ReadOnly label="Sheet" value={`${JCARD_W} × ${JCARD_H}`} />
          <ReadOnly label="Resolution" value="320 units / in" />
          <ReadOnly label="Flap" value={`0–${JCARD_PARTS.flap.w}`} />
          <ReadOnly label="Spine" value={`${JCARD_PARTS.spine.x}–${JCARD_PARTS.spine.x + JCARD_PARTS.spine.w}`} />
          <ReadOnly label="Cover" value={`${JCARD_PARTS.cover.x}–${JCARD_W}`} />
          <ReadOnly label="Safe square" value={`${JCARD_SAFE.x},${JCARD_SAFE.y} ${JCARD_SAFE.w}×${JCARD_SAFE.h}`} />
        </div>
        <Field className="gap-1.5">
          <FieldLabel className="text-xs text-muted-foreground">Focus</FieldLabel>
          <ToggleGroup
            value={[ed.focus]}
            onValueChange={(v) => ed.setFocus((v[0] as JCardFocus) || "sheet")}
            variant="outline"
            size="sm"
            spacing={0}
            aria-label="J-card focus"
            className="w-full"
          >
            {JCARD_FOCUS.map((f) => (
              <ToggleGroupItem key={f.value} value={f.value} className="flex-1 px-1 text-xs">
                {f.label}
              </ToggleGroupItem>
            ))}
          </ToggleGroup>
          <p className="text-xs text-muted-foreground">
            The focus dims everything outside the part being authored. The folds are guides:
            art may cross one.
          </p>
        </Field>
      </Section>

      <Section title="Colours">
        <p className="text-xs text-muted-foreground">
          The container&apos;s eleven roles: the label and the J-card paint from one palette.
        </p>
        <div className="grid grid-cols-2 gap-2">
          <ColourCell label="Ink" value={ed.tpl.inks.ink} onChange={(v) => ed.setRoleColor("ink", v)} />
          <ColourCell label="Rule" value={ed.tpl.inks.rule} onChange={(v) => ed.setRoleColor("rule", v)} />
          {ed.tpl.inks.band.map((c, i) => (
            <ColourCell
              key={i}
              label={`Band ${i}`}
              value={c}
              onChange={(v) => ed.setRoleColor(`band.${i}`, v)}
            />
          ))}
        </div>
        <p className="text-xs text-muted-foreground">
          {ed.jcard.prims.filter((o) => o.role?.startsWith("band.")).length} J-card layers use a
          band · {ed.jcard.prims.filter((o) => o.role === "ink").length} use the ink. The sheet is
          the paper role.
        </p>
      </Section>

      <Section title="Type">
        <p className="text-xs text-muted-foreground">
          The label&apos;s three faces at the J-card&apos;s own sizes, in units (4–640).
        </p>
        {FROLES.map((role) => {
          const f = ed.jcard.fonts[role];
          return (
            <div key={role} className="flex flex-col gap-1.5 rounded-md border border-border p-2">
              <span className="text-xs uppercase tracking-[0.08em] text-muted-foreground">
                {role}
              </span>
              <Select
                value={f.ff}
                onValueChange={(v) => v && ed.setJCardFont(role, { ff: v })}
                items={JCARD_FACES.map((face) => ({ value: face, label: face }))}
              >
                <SelectTrigger size="sm" className="w-full" aria-label={`J-card ${role} face`}>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent alignItemWithTrigger={false}>
                  {JCARD_FACES.map((face) => (
                    <SelectItem key={face} value={face}>
                      {face}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
              <Num
                label={`Size ${role} (units)`}
                value={f.size}
                min={4}
                max={640}
                onChange={(v) => ed.setJCardFont(role, { size: v })}
              />
            </div>
          );
        })}
      </Section>

      <Section title="Fields">
        <p className="text-xs text-muted-foreground">
          What the App passes for each J-card field. These are the editor&apos;s test values;
          the App supplies the album&apos;s.
        </p>
        {JCARD_FIELDS.map((field) => (
          <Field key={field} className="gap-1">
            <FieldLabel className="text-xs text-muted-foreground">
              {JCARD_FIELD_LABELS[field]}
            </FieldLabel>
            <Input
              value={ed.jcardSim(field)}
              onChange={(e) => ed.setJCardSim(field, e.target.value)}
              className="h-7 text-xs"
              aria-label={`J-card ${JCARD_FIELD_LABELS[field]} test value`}
            />
          </Field>
        ))}
      </Section>

      <Section title="Template">
        <Field className="gap-1.5">
          <FieldLabel className="text-xs text-muted-foreground">ID</FieldLabel>
          <Input
            value={ed.tpl.id}
            onChange={(e) => ed.setTplField("id", e.target.value)}
            className="h-8 font-mono text-xs"
            spellCheck={false}
            aria-label="Template id"
          />
        </Field>
        <Field className="gap-1.5">
          <FieldLabel className="text-xs text-muted-foreground">Name</FieldLabel>
          <Input
            value={ed.tpl.name}
            onChange={(e) => ed.setTplField("name", e.target.value)}
            className="h-8"
            aria-label="Template name"
          />
        </Field>
      </Section>
    </SectionGroup>
  );
}

function JCardLayerPanel({ o }: { o: Obj }) {
  const ed = useEditor();
  const locked = !!o.locked;
  const isText = o.type === "text";
  const dynamic = isText && !o.static;
  const jcRole = (role: string) => ed.jcard.fonts[FROLES.includes(role as never) ? (role as (typeof FROLES)[number]) : "caption"];
  return (
    <SectionGroup initial={isText ? "Content" : "Layout"}>
      {isText ? (
        <Section title="Content">
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Kind</FieldLabel>
            <Select
              value={dynamic ? "dynamic" : "static"}
              onValueChange={(v) => {
                if (v === "static") {
                  if (!o.text) o.text = ed.simText(o);
                  o.static = true;
                } else {
                  delete o.static;
                }
                ed.commit(false);
                ed.emit();
              }}
              items={[
                { value: "dynamic", label: "Dynamic text (from a J-card field)" },
                { value: "static", label: "Static text" },
              ]}
            >
              <SelectTrigger size="sm" className="w-full" disabled={locked} aria-label="Text kind">
                <SelectValue />
              </SelectTrigger>
              <SelectContent alignItemWithTrigger={false}>
                <SelectItem value="dynamic">Dynamic text (from a J-card field)</SelectItem>
                <SelectItem value="static">Static text</SelectItem>
              </SelectContent>
            </Select>
          </Field>
          {dynamic ? (
            <>
              <Field className="gap-1.5">
                <FieldLabel className="text-xs text-muted-foreground">Field</FieldLabel>
                <Select
                  value={o.name || ""}
                  onValueChange={(v) => {
                    if (!v) return;
                    o.name = v;
                    delete o.static;
                    delete o.sim;
                    ed.commit(false);
                    ed.emit();
                  }}
                  items={JCARD_FIELDS.map((f) => ({ value: f, label: JCARD_FIELD_LABELS[f] }))}
                >
                  <SelectTrigger size="sm" className="w-full" disabled={locked} aria-label="J-card field">
                    <SelectValue placeholder="Choose a field…" />
                  </SelectTrigger>
                  <SelectContent alignItemWithTrigger={false}>
                    {JCARD_FIELDS.map((f) => (
                      <SelectItem key={f} value={f}>
                        {JCARD_FIELD_LABELS[f]}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
                {!o.name ? (
                  <p className="text-xs text-muted-foreground">
                    Choose a field: a dynamic text with none prints nothing.
                  </p>
                ) : null}
              </Field>
              <Field className="gap-1.5">
                <FieldLabel className="text-xs text-muted-foreground">Test value</FieldLabel>
                <Input
                  value={o.sim ?? ""}
                  placeholder={ed.jcardSim(o.name as JCardField)}
                  onChange={(e) => {
                    if (e.target.value.trim() === "") delete o.sim;
                    else o.sim = e.target.value;
                    ed.commit(true);
                    ed.emit();
                  }}
                  disabled={locked}
                  className="h-7 text-xs"
                  aria-label="Test value"
                />
              </Field>
            </>
          ) : (
            <Field className="gap-1.5">
              <FieldLabel className="text-xs text-muted-foreground">Text</FieldLabel>
              <Input
                value={o.text ?? ""}
                placeholder="What this text prints"
                onChange={(e) => {
                  o.text = e.target.value;
                  ed.commit(false);
                  ed.emit();
                }}
                disabled={locked}
                className="h-8"
                aria-label="Text"
              />
              {!(o.text ?? "").trim() ? (
                <p className="text-xs text-muted-foreground">
                  Empty prints nothing. Type the string here, or set Kind to a dynamic text and
                  choose a field.
                </p>
              ) : null}
            </Field>
          )}
        </Section>
      ) : null}

      <Section title="Layout">
        <div className="grid grid-cols-2 gap-2">
          <Num label="X" value={o.x} onChange={(v) => ed.setObjField(o, "x", v)} disabled={locked} />
          <Num label="Y" value={o.y} onChange={(v) => ed.setObjField(o, "y", v)} disabled={locked} />
          <Num
            label={isText ? "Wrap width" : "Width"}
            value={o.w}
            min={1}
            onChange={(v) => ed.setObjField(o, "w", v)}
            disabled={locked}
          />
          {isText ? (
            <ReadOnly label="Height" value={`${Math.round(ed.pxOf(o) * 1.2)} auto`} />
          ) : (
            <Num
              label="Height"
              value={o.h ?? 0}
              min={1}
              onChange={(v) => ed.setObjField(o, "h", v)}
              disabled={locked}
            />
          )}
          {o.type === "rect" ? (
            <Num
              label="Radius"
              value={o.r ?? 0}
              min={0}
              onChange={(v) => ed.setObjField(o, "r", v)}
              disabled={locked}
            />
          ) : null}
          <Num
            label="Rotation °"
            value={Math.round(o.rot ?? 0)}
            onChange={(v) => ed.setObjField(o, "rot", v)}
            disabled={locked}
          />
        </div>
      </Section>

      {isText ? (
        <Section title="Typography">
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Font role</FieldLabel>
            <Select
              value={o.fr ?? "caption"}
              onValueChange={(v) => ed.setObjField(o, "fr", v as Obj["fr"])}
              items={FROLES.map((f) => ({ value: f, label: f }))}
            >
              <SelectTrigger size="sm" className="w-full" disabled={locked} aria-label="Font role">
                <SelectValue />
              </SelectTrigger>
              <SelectContent alignItemWithTrigger={false}>
                {FROLES.map((f) => {
                  const face = jcRole(f);
                  return (
                    <SelectItem key={f} value={f}>
                      {f} · {face.ff} {face.size}
                    </SelectItem>
                  );
                })}
              </SelectContent>
            </Select>
          </Field>
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Align</FieldLabel>
            <ToggleGroup
              value={[o.align ?? "left"]}
              onValueChange={(v) => {
                if (v[0]) ed.setObjField(o, "align", v[0] as Obj["align"]);
              }}
              variant="outline"
              size="sm"
              spacing={0}
              aria-label="Text align"
              className="w-full"
            >
              <ToggleGroupItem value="left" className="flex-1" aria-label="Align left">
                <AlignLeft />
              </ToggleGroupItem>
              <ToggleGroupItem value="center" className="flex-1" aria-label="Align centre">
                <AlignCenter />
              </ToggleGroupItem>
              <ToggleGroupItem value="right" className="flex-1" aria-label="Align right">
                <AlignRight />
              </ToggleGroupItem>
            </ToggleGroup>
          </Field>
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Colour role</FieldLabel>
            <RoleSelect
              value={o.role ?? "ink"}
              roles={TEXT_ROLES}
              onChange={(v) => ed.setObjField(o, "role", v)}
              disabled={locked}
            />
          </Field>
        </Section>
      ) : (
        <Section title="Appearance">
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Colour role</FieldLabel>
            <RoleSelect
              value={o.role ?? "band.0"}
              roles={ROLES}
              onChange={(v) => ed.setObjField(o, "role", v)}
              disabled={locked}
            />
          </Field>
          {o.role ? (
            <>
              <ColourRow
                label={`Colour of "${o.role}"`}
                value={ed.roleColor(o.role) ?? "#000000"}
                onChange={(v) => ed.setRoleColor(o.role!, v)}
              />
              <SwatchStrip
                value={ed.roleColor(o.role) ?? ""}
                onPick={(c) => ed.setRoleColor(o.role!, c)}
              />
            </>
          ) : null}
        </Section>
      )}

      {isText ? (
        <Section title="Appearance">
          <ColourRow
            label={o.role ? `Colour of "${o.role}"` : "Colour"}
            value={o.role ? (ed.roleColor(o.role) ?? "#000000") : "#000000"}
            onChange={(v) => (o.role ? ed.setRoleColor(o.role, v) : undefined)}
          />
          {o.role ? (
            <SwatchStrip
              value={ed.roleColor(o.role) ?? ""}
              onPick={(c) => ed.setRoleColor(o.role!, c)}
            />
          ) : null}
        </Section>
      ) : null}

      <Section title="Layer">
        <Field className="gap-1.5">
          <FieldLabel className="text-xs text-muted-foreground">Name</FieldLabel>
          <Input
            value={o.label ?? ""}
            placeholder={`${o.type} ${o.id}`}
            onChange={(e) => {
              o.label = e.target.value;
              ed.commit(false);
              ed.emit();
            }}
            className="h-8"
            aria-label="Layer name"
          />
        </Field>
        <label className="flex items-center gap-2 text-xs">
          <Switch checked={locked} onCheckedChange={() => ed.toggleLock(o)} />
          Locked
          <span className="text-muted-foreground">· no edits, drags or deletes</span>
        </label>
      </Section>
    </SectionGroup>
  );
}

function JCardMultiPanel() {
  const ed = useEditor();
  const ids = ed.selIds;
  const selected = ed.activeObjs().filter((o) => ids.includes(o.id));
  const bulkRole = (v: string) => {
    selected.forEach((o) => {
      if (o.locked) return;
      if (o.type === "text") {
        if (TEXT_ROLES.includes(v)) o.role = v;
        return;
      }
      if (v !== "ink") o.role = v;
    });
    ed.commit(false);
    ed.emit();
  };
  return (
    <SectionGroup initial="Selection">
      <Section title="Selection">
        <div className="flex flex-wrap items-center gap-1.5">
          <Button variant="destructive" size="sm" onClick={() => ed.delSel()}>
            <Trash2 />
            Delete
          </Button>
        </div>
      </Section>
      <Section title="Colour role">
        <p className="text-xs text-muted-foreground">
          Set the role of every selected J-card layer. A text takes the ink or a band; a shape
          takes any role.
        </p>
        <Select
          value=""
          onValueChange={(v) => v && bulkRole(v)}
          items={["", ...ROLES].map((r) => ({ value: r, label: r || "Set role…" }))}
        >
          <SelectTrigger size="sm" className="w-full" aria-label="Set the role of the selection">
            <SelectValue placeholder="Set role of selection…" />
          </SelectTrigger>
          <SelectContent alignItemWithTrigger={false}>
            {ROLES.map((r) => (
              <SelectItem key={r} value={r}>
                {r}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </Section>
      <Section title="Layers">
        <div className="flex flex-col gap-1">
          {selected.map((o) => (
            <button
              key={o.id}
              type="button"
              className="flex items-center gap-2 rounded-md px-1.5 py-1 text-left text-xs transition-colors hover:bg-k7-surface"
              onClick={() => ed.select(o)}
            >
              <span className="truncate">{o.label || o.name || `${o.type} ${o.id}`}</span>
              <Badge variant="outline" className="ml-auto shrink-0">
                {o.type}
              </Badge>
            </button>
          ))}
        </div>
      </Section>
    </SectionGroup>
  );
}

function PreviewFields() {
  const ed = useEditor();
  const texts = ed.objs.filter((o) => o.type === "text" && !o.static);
  if (!texts.length)
    return (
      <Empty className="m-3 border">
        <EmptyHeader>
          <EmptyMedia variant="icon">
            <MousePointerClick />
          </EmptyMedia>
          <EmptyTitle>No fields on the canvas</EmptyTitle>
          <EmptyDescription>
            Give a text a field in Design, and it appears here as a test value.
          </EmptyDescription>
        </EmptyHeader>
      </Empty>
    );
  return (
    <SectionGroup initial="Fields">
      <Section title="Fields">
      <p className="text-xs text-muted-foreground">
        The values the panel previews. Real values come from the resolver.
      </p>
      {texts.map((o) => (
        <Field key={o.id} className="gap-1">
          <FieldLabel className="text-xs text-muted-foreground">
            {slotOf(o.name)?.label ?? o.label ?? "Text"}
          </FieldLabel>
          <Input
            value={ed.eff(o).text}
            onChange={(e) => ed.setRunValue(o.id, "text", e.target.value)}
            className="h-8"
            aria-label={`Test value for ${slotOf(o.name)?.label ?? o.label ?? "text"}`}
          />
        </Field>
      ))}
      </Section>
    </SectionGroup>
  );
}

function MaskPanel() {
  const ed = useEditor();
  const n = ed.maskRange();
  return (
    <SectionGroup initial="Mask">
      <Section title="Mask">
        <ColourRow label="Colour" value={ed.mask?.color ?? "#ffffff"} onChange={(v) => ed.setMaskField("color", v)} />
        <Num
          label="Corner radius"
          value={ed.mask?.radius ?? 0}
          min={0}
          onChange={(v) => ed.setMaskField("radius", v)}
        />
        <div className="flex items-center justify-between rounded-md border border-border px-2 py-1.5 text-xs">
          <span className="text-muted-foreground">Size</span>
          <span className="tabnum">
            {PRINT_W} × {PRINT_H}
          </span>
        </div>
        <div className="flex items-center justify-between rounded-md border border-border px-2 py-1.5 text-xs">
          <span className="text-muted-foreground">Layers inside</span>
          <span className="tabnum">{n ? n.hi - n.lo + 1 : 0}</span>
        </div>
        <SwatchStrip value={ed.mask?.color ?? ""} onPick={(c) => ed.setMaskField("color", c)} />
        <Button variant="outline" size="sm" onClick={() => ed.removeMask()}>
          <Trash2 />
          Remove LayerMask
        </Button>
      </Section>
      <Section title="Aperture">
        {ed.apOf() ? (
          <p className="text-xs text-muted-foreground">
            The aperture cuts the Card's window, {WIN.x}, {WIN.y} {WIN.w}×{WIN.h} r{WIN.r},
            out of the paper. It is a placement aid; the mask already cuts the window.
          </p>
        ) : (
          <p className="text-xs text-muted-foreground">
            No aperture layer. Add a rectangle covering {WIN.x}, {WIN.y} {WIN.w}×{WIN.h} and
            tick Aperture in Appearance if the design needs the Card's window marked.
          </p>
        )}
      </Section>
    </SectionGroup>
  );
}

function MultiPanel() {
  const ed = useEditor();
  const ids = ed.selIds;
  const selected = ed.objs.filter((o) => ids.includes(o.id));
  const bulkRole = (v: string) => {
    ed.objs.forEach((o) => {
      if (!ids.includes(o.id) || o.locked || o.ap) return;
      if (o.type === "text") {
        if (TEXT_ROLES.includes(v)) o.role = v;
        return;
      }
      if (v !== "ink") o.role = v;
    });
    ed.commit(false);
    ed.emit();
  };
  return (
    <SectionGroup initial="Selection">
      <Section title="Selection">
        <div className="flex flex-wrap items-center gap-1.5">
          <Button variant="outline" size="sm" onClick={() => ed.groupSel()} disabled={!!ed.groupWhy()}>
            <FolderPlus />
            Group
          </Button>
          <Button
            variant="outline"
            size="sm"
            onClick={() => ed.ungroupSel()}
            disabled={!selected.some((o) => o.g)}
          >
            <FolderMinus />
            Ungroup
          </Button>
          <Button
            variant="outline"
            size="sm"
            onClick={() => ed.toggleSelMembers()}
            disabled={!ed.mask || selected.some((o) => o.locked)}
          >
            <SquareDashed />
            {selected.every((o) => o.m) ? "Out of LayerMask" : "Into LayerMask"}
          </Button>
          <Button variant="destructive" size="sm" onClick={() => ed.delSel()}>
            <Trash2 />
            Delete
          </Button>
        </div>
        {ed.groupWhy() ? (
          <p className="text-xs text-muted-foreground">{ed.groupWhy()}</p>
        ) : null}
      </Section>
      <Section title="Colour role">
        <p className="text-xs text-muted-foreground">
          Set the role of every selected layer. A text takes the ink or a band; a shape
          takes any role.
        </p>
        <Select
          value=""
          onValueChange={(v) => v && bulkRole(v)}
          items={["", ...ROLES].map((r) => ({ value: r, label: r || "Set role…" }))}
        >
          <SelectTrigger size="sm" className="w-full" aria-label="Set the role of the selection">
            <SelectValue placeholder="Set role of selection…" />
          </SelectTrigger>
          <SelectContent alignItemWithTrigger={false}>
            {ROLES.map((r) => (
              <SelectItem key={r} value={r}>
                {r}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </Section>
      <Section title="Layers">
        <div className="flex flex-col gap-1">
          {selected.map((o) => (
            <button
              key={o.id}
              type="button"
              className="flex items-center gap-2 rounded-md px-1.5 py-1 text-left text-xs transition-colors hover:bg-k7-surface"
              onClick={() => ed.select(o)}
            >
              <span className="truncate">{o.label || o.name || `${o.type} ${o.id}`}</span>
              <Badge variant="outline" className="ml-auto shrink-0">
                {o.type}
              </Badge>
            </button>
          ))}
        </div>
      </Section>
    </SectionGroup>
  );
}

// ---- pieces ----

const SectionGroupContext = createContext<{
  open: string | null;
  setOpen: (id: string | null) => void;
} | null>(null);

/**
 * One inspector panel's sections: one open at a time, so the open section owns the whole
 * column instead of every section sharing one scrollbar. There is always one open — the
 * last one cannot be closed, so the panel never becomes an empty list of headers.
 */
function SectionGroup({
  initial,
  children,
}: {
  initial: string | null;
  children: React.ReactNode;
}) {
  const [open, setOpen] = useState<string | null>(initial);
  return (
    <SectionGroupContext.Provider value={{ open, setOpen }}>
      {children}
    </SectionGroupContext.Provider>
  );
}

function Section({
  title,
  children,
  action,
}: {
  title: string;
  children: React.ReactNode;
  action?: React.ReactNode;
}) {
  const group = useContext(SectionGroupContext);
  const isOpen = group ? group.open === title : false;
  return (
    <section className="border-b border-border/70 last:border-b-0">
      <div className="flex h-7 items-center gap-1 pr-2">
        <button
          type="button"
          className="flex min-w-0 flex-1 cursor-pointer items-center gap-1.5 px-3 text-left transition-colors hover:text-foreground"
          aria-expanded={isOpen}
          onClick={() => group?.setOpen(title)}
        >
          <ChevronDown
            className={`size-3 shrink-0 text-muted-foreground transition-transform ${isOpen ? "" : "-rotate-90"}`}
          />
          <span className="text-xs font-medium uppercase tracking-[0.1em] text-muted-foreground">
            {title}
          </span>
        </button>
        {action}
      </div>
      {isOpen ? <div className="flex flex-col gap-3 px-3 pb-3">{children}</div> : null}
    </section>
  );
}

function Num({
  label,
  value,
  onChange,
  min,
  max,
  disabled,
}: {
  label: string;
  value: number;
  onChange: (v: number) => void;
  min?: number;
  max?: number;
  disabled?: boolean;
}) {
  return (
    <Field className="gap-1">
      <FieldLabel className="text-xs text-muted-foreground">{label}</FieldLabel>
      <NumberField
        value={value}
        onValueChange={(v) => v !== null && onChange(v)}
        min={min}
        max={max}
        size="sm"
        disabled={disabled}
      >
        <NumberFieldGroup>
          <NumberFieldDecrement />
          <NumberFieldInput aria-label={label} />
          <NumberFieldIncrement />
        </NumberFieldGroup>
      </NumberField>
    </Field>
  );
}

function ReadOnly({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex flex-col gap-1">
      <span className="text-xs text-muted-foreground">{label}</span>
      <span className="flex h-7 items-center rounded-lg border border-border px-2.5 text-xs text-muted-foreground tabnum">
        {value}
      </span>
    </div>
  );
}

function ColourRow({
  label,
  value,
  onChange,
}: {
  label: string;
  value: string;
  onChange: (v: string) => void;
}) {
  return (
    <Field className="gap-1.5">
      <FieldLabel className="text-xs text-muted-foreground">{label}</FieldLabel>
      <ColorPicker
        value={value}
        onValueChange={(v) => v && onChange(v)}
        format="hex"
        swatches={PAL}
        className="w-full justify-start"
        aria-label={label}
      />
    </Field>
  );
}

function ColourCell({
  label,
  value,
  onChange,
}: {
  label: string;
  value: string;
  onChange: (v: string) => void;
}) {
  return (
    <div className="flex flex-col gap-1">
      <span className="text-xs text-muted-foreground">{label}</span>
      <ColorPicker
        value={value}
        onValueChange={(v) => v && onChange(v)}
        format="hex"
        swatches={PAL}
        className="w-full justify-start"
        aria-label={label}
      />
    </div>
  );
}

function RoleSelect({
  value,
  roles,
  onChange,
  disabled,
}: {
  value: string;
  roles: string[];
  onChange: (v: string) => void;
  disabled?: boolean;
}) {
  const ed = useEditor();
  return (
    <Select
      value={value}
      onValueChange={(v) => v && onChange(v)}
      items={roles.map((r) => ({ value: r, label: r }))}
      disabled={disabled}
    >
      <SelectTrigger size="sm" className="w-full">
        <SelectValue />
      </SelectTrigger>
      <SelectContent alignItemWithTrigger={false}>
        {roles.map((r) => (
          <SelectItem key={r} value={r}>
            <span
              className="size-2.5 shrink-0 rounded-full border border-border"
              style={{ background: ed.roleColor(r) ?? "transparent" }}
            />
            {r}
          </SelectItem>
        ))}
      </SelectContent>
    </Select>
  );
}

export function SwatchStrip({
  value,
  onPick,
}: {
  value: string;
  onPick: (c: string) => void;
}) {
  return (
    <div className="grid grid-cols-8 gap-1" role="group" aria-label="Palette">
      {PALETTE.map((p) => (
        <button
          key={p.c}
          type="button"
          title={p.n}
          aria-label={p.n}
          aria-pressed={value.toLowerCase() === p.c.toLowerCase()}
          onClick={() => onPick(p.c)}
          className={`aspect-square w-full rounded-full border transition-colors ${
            value.toLowerCase() === p.c.toLowerCase()
              ? "border-primary ring-2 ring-primary/40"
              : "border-border hover:border-foreground/50"
          }`}
          style={{ background: p.c }}
        />
      ))}
    </div>
  );
}

