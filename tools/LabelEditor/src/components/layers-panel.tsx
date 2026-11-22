import { useRef, useState } from "react";
import {
  ChevronDown,
  ChevronRight,
  Circle,
  FolderPlus,
  Layers2,
  Lock,
  LockOpen,
  Eye,
  EyeOff,
  Square,
  Trash2,
  Type,
} from "lucide-react";

import { Badge } from "@/components/reui/badge";
import { Button } from "@/components/ui/button";
import { Empty, EmptyHeader, EmptyTitle, EmptyDescription, EmptyContent } from "@/components/ui/empty";
import { Input } from "@/components/ui/input";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import { useEditor } from "@/hooks/use-editor";
import type { Group, Obj } from "@/engine/types";

type Row =
  | {
      kind: "group";
      depth: number;
      g: Group;
      lo: number;
      hi: number;
      count: number;
      locked: boolean;
      /** Every member is hidden: the group's eye is off. */
      hidden: boolean;
    }
  | { kind: "layer"; depth: number; o: Obj; i: number; locked: boolean; gid?: string };

interface DragBlock {
  lo: number;
  hi: number;
  group: boolean;
}

interface DropTarget {
  lo: number;
  hi: number;
  gid?: string | null;
  pos: "above" | "below" | "into";
}

/** The layer stack, top layer first: groups are runs of the array. */
export function LayersPanel() {
  const ed = useEditor();
  const [drag, setDrag] = useState<DragBlock | null>(null);
  const [drop, setDrop] = useState<DropTarget | null>(null);
  const [editing, setEditing] = useState<{ kind: "layer" | "group"; id: string | number } | null>(
    null,
  );

  const rows = buildRows(ed);
  const count = ed.activeObjs().length;

  const join = (li: { lo: number; hi: number }, blk: DragBlock, above: boolean) => {
    if (li.lo <= blk.hi && li.hi >= blk.lo) return null;
    return above ? li.hi + 1 : li.lo;
  };

  const planFor = (row: Row, pos: DropTarget["pos"]): DropTarget | null => {
    if (!drag) return null;
    if (row.kind === "group" && pos === "into") {
      if (drag.group) return null;
      return { lo: row.lo, hi: row.hi, gid: row.g.id, pos };
    }
    const li =
      row.kind === "group" ? { lo: row.lo, hi: row.hi } : { lo: row.i, hi: row.i };
    const ins = join(li, drag, pos === "above");
    if (ins === null) return null;
    // Above or below a layer inside a group joins that group; beside a group header
    // leaves one (or keeps the block's own membership).
    const gid = drag.group ? undefined : row.kind === "layer" ? (row.gid ?? null) : null;
    return { lo: li.lo, hi: li.hi, pos, gid };
  };

  const commitDrop = (target: DropTarget) => {
    if (!drag) return;
    if (target.pos === "into" && target.gid) {
      ed.moveBlock(drag.lo, drag.hi, target.lo, target.gid);
      return;
    }
    const ins = target.pos === "above" ? target.hi + 1 : target.lo;
    ed.moveBlock(drag.lo, drag.hi, ins, target.gid);
  };

  const rowDragProps = (row: Row) => {
    const draggable = row.kind === "group" ? !row.locked : !row.o.locked;
    return {
      draggable,
      onDragStart: (e: React.DragEvent) => {
        if (!draggable) {
          e.preventDefault();
          return;
        }
        const blk =
          row.kind === "group"
            ? { lo: row.lo, hi: row.hi, group: true }
            : { lo: row.i, hi: row.i, group: false };
        setDrag(blk);
        e.dataTransfer.effectAllowed = "move";
        e.dataTransfer.setData("text/plain", "layer");
      },
      onDragEnd: () => {
        setDrag(null);
        setDrop(null);
      },
      onDragOver: (e: React.DragEvent) => {
        const el = e.currentTarget as HTMLElement;
        const r = el.getBoundingClientRect();
        const into = row.kind !== "layer" && e.clientY > r.top + r.height * 0.3 && e.clientY < r.bottom - r.height * 0.3;
        const pos: DropTarget["pos"] = into
          ? "into"
          : e.clientY < r.top + r.height / 2
            ? "above"
            : "below";
        const plan = planFor(row, pos);
        if (!plan) {
          setDrop(null);
          return;
        }
        e.preventDefault();
        setDrop(plan);
      },
      onDragLeave: (e: React.DragEvent) => {
        if (!(e.currentTarget as HTMLElement).contains(e.relatedTarget as Node))
          setDrop(null);
      },
      onDrop: (e: React.DragEvent) => {
        e.preventDefault();
        const el = e.currentTarget as HTMLElement;
        const r = el.getBoundingClientRect();
        const into = row.kind !== "layer" && e.clientY > r.top + r.height * 0.3 && e.clientY < r.bottom - r.height * 0.3;
        const pos: DropTarget["pos"] = into
          ? "into"
          : e.clientY < r.top + r.height / 2
            ? "above"
            : "below";
        const plan = planFor(row, pos);
        if (plan) commitDrop(plan);
        setDrag(null);
        setDrop(null);
      },
    };
  };

  const dropMark = (row: Row) => {
    if (!drop) return null;
    const same =
      (row.kind === "group" && drop.lo === row.lo && !drop.gid) ||
      (row.kind === "layer" && drop.lo === row.i);
    if (!same) return null;
    if (drop.pos === "into")
      return "shadow-[inset_0_0_0_1.5px_var(--k7-accent)]";
    return drop.pos === "above"
      ? "shadow-[inset_0_1.5px_0_var(--k7-accent)]"
      : "shadow-[inset_0_-1.5px_0_var(--k7-accent)]";
  };

  return (
    <aside
      className="flex min-h-0 w-full min-w-0 flex-col overflow-hidden border-r border-border bg-k7-bg"
      aria-label="Layers"
    >
      <div className="flex h-11 shrink-0 items-center gap-2 border-b border-border px-3">
        <Layers2 className="size-4 text-muted-foreground" />
        <h2 className="text-xs font-medium tracking-tight text-foreground">Layers</h2>
        <Badge variant="outline" className="tabnum">
          {count}
        </Badge>
      </div>

      <div className="min-h-0 flex-1 overflow-y-auto py-1">
        {rows.length === 0 ? (
          <Empty className="border-0 p-4">
            <EmptyHeader>
              <EmptyTitle>No layers yet</EmptyTitle>
              <EmptyDescription>Add a rectangle or a text to start the design.</EmptyDescription>
            </EmptyHeader>
            <EmptyContent>
              <div className="flex items-center gap-1.5">
                <Button size="sm" variant="outline" onClick={() => ed.add("rect", { x: 40, y: 40 })}>
                  Rectangle
                </Button>
                <Button size="sm" variant="outline" onClick={() => ed.add("text", { x: 40, y: 40 })}>
                  Text
                </Button>
              </div>
            </EmptyContent>
          </Empty>
        ) : null}

        <div role="tree" aria-label="Layer stack">
          {rows.map((row) => {
            const mark = dropMark(row);
            const pad = 8 + row.depth * 14;
            if (row.kind === "group") {
              const renaming = editing?.kind === "group" && editing.id === row.g.id;
              return (
                <div
                  key={row.g.id}
                  role="treeitem"
                  aria-expanded={!row.g.collapsed}
                  aria-selected={ed.selGroup === row.g.id}
                >
                  <RowShell
                    className={`${ed.selGroup === row.g.id ? "bg-k7-surface text-foreground" : ""} border-t border-border/60 ${mark ?? ""}`}
                    style={{ paddingLeft: pad }}
                    onClick={() => ed.selectGroup(row.g.id)}
                    onDoubleClick={() => setEditing({ kind: "group", id: row.g.id })}
                    {...rowDragProps(row)}
                  >
                    <button
                      type="button"
                      className="grid size-4 shrink-0 place-items-center text-muted-foreground"
                      aria-label={row.g.collapsed ? "Expand the group" : "Collapse the group"}
                      onClick={(e) => {
                        e.stopPropagation();
                        ed.toggleGroupCollapsed(row.g.id);
                      }}
                    >
                      {row.g.collapsed ? (
                        <ChevronRight className="size-3.5" />
                      ) : (
                        <ChevronDown className="size-3.5" />
                      )}
                    </button>
                    <FolderPlus className="size-3.5 shrink-0 text-muted-foreground" />
                    {renaming ? (
                      <InlineName
                        value={row.g.name}
                        onCommit={(v) => {
                          ed.renameGroup(row.g.id, v);
                          setEditing(null);
                        }}
                        onCancel={() => setEditing(null)}
                      />
                    ) : (
                      <span className="truncate text-xs font-medium">{row.g.name}</span>
                    )}
                    <span className="ml-auto text-xs text-muted-foreground tabnum">{row.count}</span>
                    <IconAction
                      label={row.hidden ? "Show the group" : "Hide the group"}
                      reveal
                      onClick={(e) => {
                        e.stopPropagation();
                        ed.toggleHiddenGroup(row.g.id);
                      }}
                      className="ml-1"
                    >
                      {row.hidden ? <EyeOff /> : <Eye />}
                    </IconAction>
                    <IconAction
                      label={row.locked ? "Unlock the group" : "Lock the group"}
                      reveal
                      onClick={(e) => {
                        e.stopPropagation();
                        ed.groupLock(row.g.id);
                      }}
                    >
                      {row.locked ? <Lock /> : <LockOpen />}
                    </IconAction>
                  </RowShell>
                </div>
              );
            }
            const o = row.o;
            const selected = o === ed.sel;
            const multi = !selected && ed.selSet.has(o.id);
            const renaming = editing?.kind === "layer" && editing.id === o.id;
            const Icon = o.type === "text" ? Type : o.type === "circle" ? Circle : Square;
            return (
              <div
                key={o.id}
                role="treeitem"
                aria-selected={selected || multi}
                tabIndex={0}
                onKeyDown={(e) => {
                  if (e.key === "Enter") ed.select(o);
                  if (e.key === "Delete" || e.key === "Backspace") {
                    e.preventDefault();
                    ed.select(o);
                    ed.delSel();
                  }
                }}
              >
                <RowShell
                  className={`${selected || multi ? "bg-k7-surface text-foreground" : ""} ${mark ?? ""} ${o.locked ? "opacity-60" : ""}`}
                  style={{ paddingLeft: pad }}
                  onClick={(e) =>
                    ed.select(o, { additive: e.metaKey || e.ctrlKey || e.shiftKey })
                  }
                  onDoubleClick={() => setEditing({ kind: "layer", id: o.id })}
                  {...rowDragProps(row)}
                >
                  <Icon className="size-3.5 shrink-0 text-muted-foreground" />
                  {renaming ? (
                    <InlineName
                      value={o.label || o.name || ""}
                      placeholder="Layer name"
                      onCommit={(v) => {
                        o.label = v.trim();
                        ed.commit(false);
                        setEditing(null);
                      }}
                      onCancel={() => setEditing(null)}
                    />
                  ) : (
                    <span className="truncate text-xs">
                      {layerNameOf(o)}
                    </span>
                  )}
                  {o.name ? (
                    <Badge variant="outline" className="shrink-0">
                      {ed.isJCard() ? "field" : "input"}
                    </Badge>
                  ) : null}
                  {o.type === "text" && o.static && !(o.text ?? "").trim() ? (
                    <Badge variant="outline" className="shrink-0" title="Prints nothing">
                      empty
                    </Badge>
                  ) : null}
                  {ed.outOfSpace(o) ? (
                    <Badge variant="outline" className="shrink-0" title="Lies outside the print">
                      bleed
                    </Badge>
                  ) : null}
                  <IconAction
                    label={o.hidden ? "Show" : "Hide"}
                    onClick={(e) => {
                      e.stopPropagation();
                      ed.toggleHidden(o);
                    }}
                    className="ml-auto"
                  >
                    {o.hidden ? <EyeOff /> : <Eye />}
                  </IconAction>
                  <IconAction
                    label={o.locked ? "Unlock" : "Lock"}
                    reveal
                    onClick={(e) => {
                      e.stopPropagation();
                      ed.toggleLock(o);
                    }}
                  >
                    {o.locked ? <Lock /> : <LockOpen />}
                  </IconAction>
                </RowShell>
              </div>
            );
          })}
        </div>
      </div>

      {/* The actions sit at the bottom, where a hand lands after dragging a row. */}
      <div
        data-layers-actions="1"
        className="flex h-10 shrink-0 items-center justify-end gap-0.5 border-t border-border px-3"
      >
        <IconAction
          label={ed.groupWhy() || "Group selected (⌘G)"}
          disabled={!!ed.groupWhy()}
          onClick={() => ed.groupSel()}
        >
          <FolderPlus />
        </IconAction>
        <IconAction label="Delete (⌫)" disabled={!ed.sel} onClick={() => ed.delSel()}>
          <Trash2 />
        </IconAction>
      </div>
    </aside>
  );
}

/** A layer's row label: its name, its field, or (for a static text) what it prints. */
function layerNameOf(o: Obj): string {
  if (o.label) return o.label;
  if (o.name) return o.name;
  if (o.type === "text" && o.static && o.text?.trim()) return o.text.trim();
  return `${o.type} ${o.id}`;
}

function buildRows(ed: ReturnType<typeof useEditor>): Row[] {
  const rows: Row[] = [];
  const list = ed.activeObjs();
  let i = list.length - 1;
  while (i >= 0) {
    const o = list[i];
    const g = o.g ? ed.groups.find((x) => x.id === o.g) : null;
    if (!g) {
      rows.push({
        kind: "layer",
        depth: 0,
        o,
        i,
        locked: !!o.locked,
      });
      i--;
      continue;
    }
    const b = ed.blockOf(o);
    const mem = list.slice(b.lo, b.hi + 1);
    rows.push({
      kind: "group",
      depth: 0,
      g,
      lo: b.lo,
      hi: b.hi,
      count: mem.length,
      locked: mem.every((m) => m.locked),
      hidden: mem.every((m) => !!m.hidden),
    });
    if (!g.collapsed)
      for (let k = b.hi; k >= b.lo; k--)
        rows.push({
          kind: "layer",
          depth: 1,
          o: list[k],
          i: k,
          locked: !!list[k].locked,
          gid: g.id,
        });
    i = b.lo - 1;
  }
  return rows;
}

function RowShell({
  className = "",
  style,
  onClick,
  onDoubleClick,
  children,
  ...rest
}: React.ComponentProps<"div">) {
  return (
    <div
      className={`group/row flex h-7 cursor-default select-none items-center gap-1.5 pr-1 text-foreground transition-colors hover:bg-k7-surface/60 ${className}`}
      style={style}
      onClick={onClick}
      onDoubleClick={onDoubleClick}
      {...rest}
    >
      {children}
    </div>
  );
}

/**
 * A small icon button. In a row it is revealed by hovering the row; in the panel header it
 * is always visible, because a control you cannot see is a control you cannot find.
 */
function IconAction({
  label,
  disabled,
  onClick,
  reveal = false,
  className = "",
  children,
}: {
  label: string;
  disabled?: boolean;
  onClick?: (e: React.MouseEvent) => void;
  reveal?: boolean;
  className?: string;
  children: React.ReactNode;
}) {
  return (
    <Tooltip>
      <TooltipTrigger
        render={
          <Button
            variant="ghost"
            size="icon-xs"
            className={`${className} text-muted-foreground ${
              reveal
                ? "opacity-0 group-hover/row:opacity-100 focus-visible:opacity-100"
                : "opacity-100"
            } ${disabled ? "opacity-50 hover:bg-transparent" : "hover:text-foreground"}`}
            // Not the native `disabled`: a disabled button swallows the hover, and the
            // tooltip is where the reason it cannot be pressed is written.
            aria-disabled={disabled || undefined}
            onClick={(e) => {
              if (disabled) {
                e.preventDefault();
                e.stopPropagation();
                return;
              }
              onClick?.(e);
            }}
            aria-label={label}
          >
            {children}
          </Button>
        }
      />
      <TooltipContent>{label}</TooltipContent>
    </Tooltip>
  );
}

function InlineName({
  value,
  placeholder,
  onCommit,
  onCancel,
}: {
  value: string;
  placeholder?: string;
  onCommit: (v: string) => void;
  onCancel: () => void;
}) {
  const ref = useRef<HTMLInputElement>(null);
  return (
    <Input
      ref={ref}
      autoFocus
      defaultValue={value}
      placeholder={placeholder}
      className="h-5 min-w-0 flex-1 px-1 text-xs"
      onClick={(e) => e.stopPropagation()}
      onDoubleClick={(e) => e.stopPropagation()}
      onKeyDown={(e) => {
        e.stopPropagation();
        if (e.key === "Enter") onCommit(ref.current?.value ?? "");
        if (e.key === "Escape") onCancel();
      }}
      onBlur={(e) => onCommit(e.target.value)}
    />
  );
}
