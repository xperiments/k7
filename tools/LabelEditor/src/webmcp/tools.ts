import type { Editor } from "@/engine/editor";
import type { Obj, Surface, Tool } from "@/engine/types";
import type { LabelTool } from "@/webmcp/types";
import { skillProse } from "./skill";
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
import { buildArtPack, PackError, toBase64 } from "@/engine/labelPack";
import {
  CANVAS_MAX,
  FROLES,
  PRINT_H,
  PRINT_W,
  SCREEN_H,
  SCREEN_W,
  SHEET_FONTS,
  SLOTS,
  WIN,
  isColour,
  onSheet,
  sheetFaces,
  sheetSizes,
  slotOf,
} from "@/engine/vocab";

// ---- schema helpers: short property builders, so every field can carry a description ----

const str = (description: string, extra: Record<string, unknown> = {}) => ({
  type: "string",
  description,
  ...extra,
});
const num = (description: string, extra: Record<string, unknown> = {}) => ({
  type: "number",
  description,
  ...extra,
});
const bool = (description: string) => ({ type: "boolean", description });
const list = (description: string, items: Record<string, unknown>) => ({
  type: "array",
  description,
  items,
});
const schema = (properties: Record<string, unknown>, required?: string[]) => ({
  type: "object" as const,
  properties,
  ...(required ? { required } : {}),
});

const HEX = /^#[0-9a-fA-F]{6}$/;
const TEMPLATE_ID = /^k7\.[a-z0-9][a-z0-9-]*$/;

// ---- argument readers: schema hints are loose, so the checks here are the strict ones ----

type Args = Record<string, unknown>;

function readNum(args: Args, key: string, min?: number, max?: number): number | undefined {
  const v = args[key];
  if (v === undefined || v === null) return undefined;
  if (typeof v !== "number" || !Number.isFinite(v))
    throw new Error(`${key} must be a number`);
  if (min !== undefined && v < min) throw new Error(`${key} must be at least ${min}`);
  if (max !== undefined && v > max) throw new Error(`${key} must be at most ${max}`);
  return v;
}

function readStr(args: Args, key: string): string | undefined {
  const v = args[key];
  if (v === undefined || v === null) return undefined;
  if (typeof v !== "string") throw new Error(`${key} must be a string`);
  return v;
}

function readBool(args: Args, key: string): boolean | undefined {
  const v = args[key];
  if (v === undefined || v === null) return undefined;
  if (typeof v !== "boolean") throw new Error(`${key} must be true or false`);
  return v;
}

function readEnum<T extends string>(
  args: Args,
  key: string,
  values: readonly T[],
): T | undefined {
  const v = readStr(args, key);
  if (v === undefined) return undefined;
  if (!values.includes(v as T))
    throw new Error(`${key} must be one of ${values.map((x) => `"${x}"`).join(", ")}`);
  return v as T;
}

function readHex(args: Args, key: string): string | undefined {
  const v = readStr(args, key);
  if (v === undefined) return undefined;
  if (!HEX.test(v)) throw new Error(`${key} must be a colour like #c68b2e`);
  return v.toLowerCase();
}

function readIds(args: Args): number[] | undefined {
  const v = args.ids;
  if (v === undefined || v === null) return undefined;
  if (!Array.isArray(v) || !v.length || v.some((n) => typeof n !== "number"))
    throw new Error("ids must be a non-empty array of layer ids from k7art-read");
  return v as number[];
}

const SURFACES = ["label", "jcard"] as const;

/** The surface a call names, or the one the person at the page is on. */
function readSurface(ed: Editor, args: Args): Surface {
  const surface = readEnum(args, "surface", SURFACES) ?? ed.surface;
  if (surface !== ed.surface) ed.setSurface(surface);
  return surface;
}

function readField(
  args: Args,
  surface: Surface,
  key = "field",
): string | undefined {
  const field = readStr(args, key);
  if (field === undefined) return undefined;
  if (surface === "jcard") {
    if (!JCARD_FIELDS.includes(field as JCardField))
      throw new Error(
        `field "${field}" is not a J-card field (${JCARD_FIELDS.join(", ")})`,
      );
    return field;
  }
  if (!slotOf(field))
    throw new Error(`field "${field}" is not one of ${SLOTS.map((s) => s.id).join(", ")}`);
  return field;
}

function readStrings(args: Args, key: string): string[] | undefined {
  const v = args[key];
  if (v === undefined || v === null) return undefined;
  if (!Array.isArray(v) || v.some((x) => typeof x !== "string"))
    throw new Error(`${key} must be an array of names or ids`);
  return v as string[];
}

function layerById(ed: Editor, id: unknown, surface: Surface): Obj {
  if (typeof id !== "number")
    throw new Error("id must be the number from k7art-read");
  const o = ed.activeObjs().find((x) => x.id === id);
  if (!o)
    throw new Error(
      `there is no ${surface === "jcard" ? "J-card " : ""}layer ${id} in the active design`,
    );
  return o;
}

/** A compact view of one layer, enough for an agent to reason and then edit it. */
function layerSummary(ed: Editor, o: Obj, surface: Surface = ed.surface) {
  return {
    id: o.id,
    type: o.type,
    layerName: o.label || null,
    field: o.name || null,
    static: !!o.static,
    text:
      o.type === "text"
        ? o.static
          ? o.text || ""
          : ed.simText(o)
        : null,
    color: o.color ?? null,
    x: Math.round(o.x),
    y: Math.round(o.y),
    width: Math.round(o.w),
    height: o.type === "text" ? Math.round(ed.pxOf(o, surface) * 1.2) : (o.h ?? 0),
    rotation: Math.round(o.rot || 0),
    locked: !!o.locked,
    hidden: !!o.hidden,
    group: ed.groups.find((g) => g.id === o.g)?.name ?? null,
  };
}

function designSummary(ed: Editor) {
  return {
    templateId: ed.tpl.id,
    templateName: ed.tpl.name,
    surface: ed.surface,
    background: ed.bg,
    backgrounds: ed.bgs,
    backgroundIndex: ed.bgi,
    mask: { colour: ed.mask.color, radius: ed.mask.radius },
    fonts: FROLES.map((role) => ({
      role,
      face: ed.tpl.fonts[role].ff,
      size: ed.tpl.fonts[role].px,
    })),
    jcardFonts: FROLES.map((role) => ({
      role,
      face: ed.jcard.fonts[role].ff,
      size: ed.jcard.fonts[role].size,
    })),
    layers: ed.objs.length,
    jcardLayers: ed.jcard.prims.length,
    errors: ed.buildTemplate().errs,
  };
}

function jcardSummary(ed: Editor) {
  return {
    space: { w: JCARD_W, h: JCARD_H, unit: 320 },
    parts: JCARD_PARTS,
    safe: JCARD_SAFE,
    fields: JCARD_FIELDS.map((field) => ({
      id: field,
      label: JCARD_FIELD_LABELS[field],
      preview: ed.jcardSim(field),
    })),
    focus: ed.focus,
    /* The sheet's own colour: the label's paper is its own, and the two are independent. */
    background: ed.jcard.background,
    typeRoles: FROLES.map((role) => ({
      role,
      face: ed.jcard.fonts[role].ff,
      size: ed.jcard.fonts[role].size,
      faces: JCARD_FACES,
    })),
    layers: ed.jcard.prims.map((o) =>
      layerSummary(ed, o as unknown as Obj, "jcard"),
    ),
    errors: ed.buildTemplate().errs,
  };
}

/** A pack id from a template id: k7.neon-skyline -> neon-skyline. */
const slugOf = (id: string) => id.replace(/^k7\./, "").replace(/[^a-z0-9-]/g, "-");

function docsSummary(ed: Editor) {
  return ed.docs.map((d, i) => ({
    name: d.name,
    templateId: d.data.tpl?.id ?? null,
    labelLayers: d.data.objs.length,
    jcardLayers: d.data.jcard?.prims.length ?? 0,
    active: i === ed.active,
  }));
}

export function labelEditorTools(
  ed: Editor,
  opts: { guide: string },
): LabelTool[] {
  return [
    {
      name: "k7art-read",
      description:
        "Read the K7Art editor: both surfaces — the label's 748x359 print, fields, roles and compiled type pairs, and the J-card's 1300x1280 sheet, folds, safe square, three fields and sizes — the active surface and focus, the open documents, both surfaces' layers, each surface's groups, the label's paper, the selection and the export errors.",
      inputSchema: schema({
        includeTemplate: bool(
          "Also return the active design as a K7Art object.",
        ),
      }),
      execute: (args) => {
        const tpl = ed.buildTemplate();
        const active = ed.activeObjs();
        return {
          ok: true,
          surface: ed.surface,
          focus: ed.focus,
          board: {
            print: { width: PRINT_W, height: PRINT_H },
            screen: { width: SCREEN_W, height: SCREEN_H },
            jcard: { width: JCARD_W, height: JCARD_H, unit: 320 },
          },
          fields: SLOTS.map((s) => ({ id: s.id, label: s.label })),
          typeRoles: FROLES.map((role) => ({
            role,
            face: ed.tpl.fonts[role].ff,
            size: ed.tpl.fonts[role].px,
            faces: sheetFaces,
            sizes: sheetSizes(ed.tpl.fonts[role].ff),
          })),
          jcard: jcardSummary(ed),
          label: {
            space: { w: PRINT_W, h: PRINT_H },
            aperture: WIN,
            canvas: { backgrounds: ed.bgs, active: ed.bgi },
            typeRoles: FROLES.map((role) => ({
              role,
              face: ed.tpl.fonts[role].ff,
              size: ed.tpl.fonts[role].px,
            })),
            fields: SLOTS.map((s) => ({ id: s.id, label: s.label })),
            layers: ed.objs.map((o) => layerSummary(ed, o, "label")),
            mask: { colour: ed.mask.color, radius: ed.mask.radius },
          },
          documents: docsSummary(ed),
          active: {
            name: ed.doc?.name ?? null,
            templateId: ed.tpl.id,
            templateName: ed.tpl.name,
            surface: ed.surface,
            background: ed.bg,
            backgrounds: ed.bgs,
            backgroundIndex: ed.bgi,
            layers: active.map((o) => layerSummary(ed, o)),
            labelLayers: ed.objs.map((o) => layerSummary(ed, o, "label")),
            jcardLayers: ed.jcard.prims.map((o) =>
              layerSummary(ed, o as unknown as Obj, "jcard"),
            ),
            mask: { colour: ed.mask.color, radius: ed.mask.radius },
            /* The active surface's groups: one list holds both surfaces', and a group is
             * only this surface's when a layer here belongs to it. */
            groups: ed.groups
              .map((g) => ({
                id: g.id,
                name: g.name,
                layers: ed
                  .activeObjs()
                  .filter((o) => o.g === g.id)
                  .map((o) => o.id),
              }))
              .filter((g) => g.layers.length > 0),
            selection: ed.selIds,
            errors: tpl.errs,
          },
          ...(args.includeTemplate === true ? { template: tpl.doc } : {}),
        };
      },
    },

    {
      name: "k7art-guide",
      description:
        "Read this once before designing: what a K7Art is for — the label's print and the J-card's sheet — how the print and the Card's window shape the label, the J-card's flap | spine | cover, the fields and the three faces, the workflow, what gets refused, and how to do all of it from the visible controls when the tools cannot be reached. The same guide is served at /k7art/skill.md.",
      inputSchema: schema({}),
      execute: () => ({
        ok: true,
        guide: skillProse(opts.guide),
        url: "/skill.md",
      }),
    },

    {
      name: "k7art-add-layer",
      description:
        "Add a layer to the active design, on the label's 748x359 print or the J-card's 1300x1280 sheet. Coordinates are design units of that surface, origin at its top-left; a layer may sit outside and still export, where the label's print crops it. A text is static unless a field is given; on the J-card the field is title, artist or meta.",
      inputSchema: schema(
        {
          surface: str(
            "Which surface to add to. Defaults to the active surface.",
            { enum: SURFACES },
          ),
          type: str("What to add.", {
            enum: ["rect", "square", "circle", "text"],
            default: "rect",
          }),
          x: num("Left edge in design units. Default 40."),
          y: num("Top edge in design units. Default 40."),
          width: num("Width in design units; for a text this is its wrap width.", {
            minimum: 1,
          }),
          height: num("Height in design units, for a shape. A text's height follows its type.", {
            minimum: 1,
          }),
          text: str(
            "The string a static text prints. Do not combine with field.",
          ),
          field: str(
            `The field a dynamic text prints. The label's: ${SLOTS.map((s) => s.id).join(", ")}. The J-card's: ${JCARD_FIELDS.join(", ")}. Do not combine with text.`,
            { enum: [...SLOTS.map((s) => s.id), ...JCARD_FIELDS] },
          ),
          color: str(
            "The layer's colour, #RRGGBB: a shape's fill or a text's type. Defaults to the swatch row / the ink.",
          ),
          fontRole: str("Which type role a text prints in.", { enum: FROLES }),
          align: str("A text's horizontal alignment.", {
            enum: ["left", "center", "right"],
          }),
          layerName: str("The layer's name in the Layers panel."),
          rotation: num("Rotation in degrees, clockwise."),
        },
        [],
      ),
      execute: (args) => {
        const surface = readSurface(ed, args);
        const type = (readEnum(args, "type", ["rect", "square", "circle", "text"] as const) ??
          "rect") as Tool;
        const x = Math.round(readNum(args, "x") ?? 40);
        const y = Math.round(readNum(args, "y") ?? 40);
        const text = readStr(args, "text");
        const field = readField(args, surface);
        const w = readNum(args, "width", 1);
        const h = readNum(args, "height", 1);
        const color = readStr(args, "color");
        const fontRole = readEnum(args, "fontRole", FROLES);
        const align = readEnum(args, "align", ["left", "center", "right"] as const);
        const layerName = readStr(args, "layerName");
        const rot = readNum(args, "rotation");
        // Validate everything before touching the document, so a refused call adds nothing.
        if (text !== undefined && field !== undefined)
          throw new Error("give text for a static text or field for a dynamic one, not both");
        if (text !== undefined && type !== "text")
          throw new Error("only a text prints a string; set type to text");
        if (field !== undefined && type !== "text")
          throw new Error("only a text prints a field; set type to text");
        if (text !== undefined && !text.trim()) throw new Error("text must not be empty");
        if (h !== undefined && type === "text")
          throw new Error("a text's height follows its type; set width (the wrap width)");
        if (color !== undefined && !isColour(color))
          throw new Error(`color for a ${type} must be #RRGGBB`);
        if (fontRole !== undefined && type !== "text")
          throw new Error("only a text has a font role");
        if (align !== undefined && type !== "text")
          throw new Error("only a text has an alignment");

        ed.add(type, { x, y });
        const o = ed.sel;
        if (!o) throw new Error("the layer could not be added");
        if (w !== undefined) o.w = Math.round(w);
        if (h !== undefined) o.h = Math.round(h);
        if (text !== undefined) {
          o.static = true;
          o.text = text;
          o.name = "";
        }
        if (field !== undefined) {
          o.name = field;
          delete o.static;
          delete o.text;
          if (surface === "jcard") delete o.sim;
        }
        if (color !== undefined) o.color = color;
        if (fontRole !== undefined) o.fr = fontRole;
        if (align !== undefined) o.align = align;
        if (layerName !== undefined) o.label = layerName;
        if (rot !== undefined) o.rot = Math.round(((rot % 360) + 360) % 360);
        ed.commit(false);
        ed.emit();
        return {
          ok: true,
          message: `Added ${o.type} ${o.id} on the ${surface}.`,
          surface,
          id: o.id,
          layer: layerSummary(ed, o),
        };
      },
    },

    {
      name: "k7art-update-layer",
      description:
        "Change one layer of the active design, on the label or the J-card: position or size in that surface's units, rotation, its text or field, colour (#RRGGBB), font role, alignment, layer name, test value, lock or visibility. Only the fields given change. A hidden layer is not drawn and not exported.",
      inputSchema: schema(
        {
          surface: str(
            "Which surface the layer is on. Defaults to the active surface.",
            { enum: SURFACES },
          ),
          id: num("The layer id from k7art-read.", { minimum: 1 }),
          x: num("Left edge in design units."),
          y: num("Top edge in design units."),
          width: num("Width in design units; a text's wrap width.", { minimum: 1 }),
          height: num("Height in design units, for a shape.", { minimum: 1 }),
          rotation: num("Rotation in degrees, clockwise."),
          text: str("Make the text static and print this string."),
          field: str(
            `Make the text dynamic and print this field. The label's: ${SLOTS.map((s) => s.id).join(", ")}. The J-card's: ${JCARD_FIELDS.join(", ")}.`,
            { enum: [...SLOTS.map((s) => s.id), ...JCARD_FIELDS] },
          ),
          color: str("The colour to print in, #RRGGBB."),
          fontRole: str("The text's type role.", { enum: FROLES }),
          align: str("The text's horizontal alignment.", {
            enum: ["left", "center", "right"],
          }),
          layerName: str("The layer's name in the Layers panel."),
          testValue: str(
            "The field's test value: what every text naming that field previews in the editor and Preview. One value per field, not per layer.",
          ),
          locked: bool("Lock or unlock the layer. Other changes need it unlocked."),
          hidden: bool(
            "Hide the layer, or show it again. A hidden layer is not drawn on the canvas, in Preview or in the App, and Export and Publish leave it out.",
          ),
        },
        ["id"],
      ),
      execute: (args) => {
        const surface = readSurface(ed, args);
        const o = layerById(ed, args.id, surface);
        const locked = readBool(args, "locked");
        const x = readNum(args, "x");
        const y = readNum(args, "y");
        const w = readNum(args, "width", 1);
        const h = readNum(args, "height", 1);
        const rot = readNum(args, "rotation");
        const text = readStr(args, "text");
        const field = readField(args, surface);
        const color = readStr(args, "color");
        const fontRole = readEnum(args, "fontRole", FROLES);
        const align = readEnum(args, "align", ["left", "center", "right"] as const);
        const layerName = readStr(args, "layerName");
        const testValue = readStr(args, "testValue");
        // Validate everything before touching the document.
        if (o.locked && locked !== false)
          throw new Error(`layer ${o.id} is locked; unlock it with locked: false first`);
        if (text !== undefined && field !== undefined)
          throw new Error("give text for a static text or field for a dynamic one, not both");
        if (text !== undefined && o.type !== "text")
          throw new Error("only a text prints a string");
        if (field !== undefined && o.type !== "text")
          throw new Error("only a text prints a field");
        if (text !== undefined && !text.trim()) throw new Error("text must not be empty");
        if (h !== undefined && o.type === "text")
          throw new Error("a text's height follows its type; set width (the wrap width)");
        if (color !== undefined && !isColour(color))
          throw new Error(`color for a ${o.type} must be #RRGGBB`);
        if (fontRole !== undefined && o.type !== "text")
          throw new Error("only a text has a font role");
        if (align !== undefined && o.type !== "text")
          throw new Error("only a text has an alignment");
        if (testValue !== undefined && o.type !== "text")
          throw new Error("only a text has a test value");

        if (locked !== undefined) o.locked = locked;
        const hidden = readBool(args, "hidden");
        if (hidden !== undefined) {
          if (hidden) o.hidden = true;
          else delete o.hidden;
        }
        if (x !== undefined) o.x = Math.round(x);
        if (y !== undefined) o.y = Math.round(y);
        if (w !== undefined) o.w = Math.round(w);
        if (h !== undefined) o.h = Math.round(h);
        if (rot !== undefined) o.rot = Math.round(((rot % 360) + 360) % 360);
        if (text !== undefined) {
          o.static = true;
          o.text = text;
          o.name = "";
        }
        if (field !== undefined) {
          o.name = field;
          delete o.static;
          delete o.text;
          if (surface === "jcard") delete o.sim;
        }
        if (color !== undefined) o.color = color;
        if (fontRole !== undefined) o.fr = fontRole;
        if (align !== undefined) o.align = align;
        if (layerName !== undefined) o.label = layerName;
        if (testValue !== undefined) {
          /* One set of test values for both surfaces: the J-card reads the label's, so a
           * value set here is the design's, wherever it is drawn. */
          ed.setSim(o.name, testValue);
        }
        ed.commit(false);
        ed.emit();
        return {
          ok: true,
          message: `Updated layer ${o.id} on the ${surface}.`,
          surface,
          layer: layerSummary(ed, o),
        };
      },
    },

    {
      name: "k7art-delete-layer",
      description: "Delete one layer of the active design, on the label or the J-card.",
      inputSchema: schema(
        {
          surface: str(
            "Which surface the layer is on. Defaults to the active surface.",
            { enum: SURFACES },
          ),
          id: num("The layer id from k7art-read.", { minimum: 1 }),
        },
        ["id"],
      ),
      execute: (args) => {
        const surface = readSurface(ed, args);
        const o = layerById(ed, args.id, surface);
        if (o.locked) throw new Error(`layer ${o.id} is locked; unlock it first`);
        ed.selectLayers([o.id], surface);
        ed.delSel();
        return {
          ok: true,
          message: `Deleted layer ${o.id} from the ${surface}.`,
          surface,
          layers: ed.activeObjs().length,
        };
      },
    },

    {
      name: "k7art-select-layer",
      description:
        "Select one or more layers on the label or the J-card, so the person at the screen sees the same selection the tools are working on. Naming the surface switches the page to it.",
      inputSchema: schema(
        {
          surface: str(
            "Which surface the layers are on. Defaults to the active surface.",
            { enum: SURFACES },
          ),
          ids: list(
            "Layer ids from k7art-read. The last one is the primary selection.",
            { type: "number" },
          ),
        },
        ["ids"],
      ),
      execute: (args) => {
        const surface = readSurface(ed, args);
        const ids = readIds(args)!;
        const missing = ids.filter((id) => !ed.activeObjs().some((o) => o.id === id));
        if (missing.length)
          throw new Error(`no layer ${missing.join(", ")} on the ${surface}`);
        ed.selectLayers(ids, surface);
        return { ok: true, surface, selection: ed.selIds };
      },
    },

    {
      name: "k7art-move-layer",
      description:
        "Move a layer through its surface's stack: one step up or down, or straight to the top or bottom. The top of the stack prints last, over the layers below it.",
      inputSchema: schema(
        {
          surface: str(
            "Which surface the layer is on. Defaults to the active surface.",
            { enum: SURFACES },
          ),
          id: num("The layer id from k7art-read.", { minimum: 1 }),
          direction: str("Which way to move it.", {
            enum: ["up", "down", "top", "bottom"],
          }),
        },
        ["id", "direction"],
      ),
      execute: (args) => {
        const surface = readSurface(ed, args);
        const o = layerById(ed, args.id, surface);
        const direction = readEnum(args, "direction", [
          "up",
          "down",
          "top",
          "bottom",
        ] as const)!;
        ed.selectLayers([o.id], surface);
        if (direction === "up" || direction === "down") ed.mv(direction === "up" ? 1 : -1);
        else {
          const b = ed.blockOf(o);
          if (direction === "top") ed.moveBlock(b.lo, b.hi, ed.activeObjs().length);
          else ed.moveBlock(b.lo, b.hi, 0);
        }
        return {
          ok: true,
          message: `Moved layer ${o.id} ${direction} on the ${surface}.`,
          surface,
          index: ed.activeObjs().findIndex((x) => x.id === o.id),
        };
      },
    },

    {
      name: "k7art-group-layers",
      description:
        "Group layers into one layer group, or ungroup a group, on either surface. Grouping needs two or more layers, and a group is a contiguous run, which the tool checks. Groups belong to the surface they were made on.",
      inputSchema: schema(
        {
          surface: str(
            "Which surface's layers to group: the label's print or the J-card's sheet. Groups do not span the two.",
            { enum: SURFACES },
          ),
          action: str("Group or ungroup. Default group.", {
            enum: ["group", "ungroup"],
            default: "group",
          }),
          ids: list("The layers to group; omit to use the current selection.", {
            type: "number",
          }),
        },
      ),
      execute: (args) => {
        const surface = readSurface(ed, args);
        const action = readEnum(args, "action", ["group", "ungroup"] as const) ?? "group";
        const ids = readIds(args);
        if (ids) ed.selectLayers(ids, surface);
        if (!ed.sel) throw new Error("nothing is selected");
        if (action === "group") {
          const why = ed.groupWhy();
          if (why) throw new Error(why);
          ed.groupSel();
        } else ed.ungroupSel();
        return {
          ok: true,
          message: action === "group" ? "Grouped the selection." : "Ungrouped the selection.",
          groups: ed.groups.map((g) => ({
            name: g.name,
            layers: ed.activeObjs()
              .filter((o) => o.g === g.id)
              .map((o) => o.id),
          })),
          selection: ed.selIds,
        };
      },
    },

    {
      name: "k7art-set-design",
      description:
        "Set the design's template id or name, its canvas background cycle (the K7 picks one by the Track's cassette row; the active one is only the editor's preview), the label's paper colour and radius (the file's `mask`), the J-card sheet's background, one of the label's type roles' face and size, or one of the J-card's three type sizes. A layer's own colour is k7art-update-layer's; every colour is #RRGGBB; a label face and size must be a pair the sheet carries; a J-card size is 4..640 units.",
      inputSchema: schema({
        templateId: str("The design id, like k7.my-design; it names the exported file."),
        name: str("The design's human name."),
        background: str(
          "The active canvas background, as #rrggbb; it sets the editor's preview preset."),
        backgrounds: list(
          `The canvas background cycle, as #rrggbb strings: one to ${CANVAS_MAX}. The K7 picks one by the Track's cassette row.`,
          { type: "string" },
        ),
        activeBackground: num("The index of the editor's preview preset.", {
          minimum: 0,
        }),
        maskColor: str(
          "The label's paper colour, as #rrggbb: the colour painted behind every layer. The paper is always present.",
        ),
        jcardBackground: str(
          "The J-card sheet's own background, as #rrggbb: the colour painted under its prims. Independent of the label's paper.",
        ),
        maskRadius: num("The paper's corner radius in design pixels.", { minimum: 0 }),
        fonts: {
          type: "object",
          description:
            `The label's type roles to change: a face from ${sheetFaces.join(", ")} and one of that face's sizes.`,
          properties: Object.fromEntries(
            FROLES.map((role) => [
              role,
              {
                type: "object",
                description: `The label's ${role} role.`,
                properties: {
                  face: str("The face name.", { enum: sheetFaces }),
                  size: num("The pixel size the sheet carries for that face."),
                },
              },
            ]),
          ),
        },
        jcardFonts: {
          type: "object",
          description:
            "The J-card's type roles to change: the same three faces at the J-card's own sizes in units (4..640).",
          properties: Object.fromEntries(
            FROLES.map((role) => [
              role,
              {
                type: "object",
                description: `The J-card's ${role} role.`,
                properties: {
                  face: str("The face name.", { enum: JCARD_FACES }),
                  size: num("The size in units, 4..640.", { minimum: 4, maximum: 640 }),
                },
              },
            ]),
          ),
        },
      }),
      execute: (args) => {
        const templateId = readStr(args, "templateId");
        if (templateId !== undefined) {
          if (!TEMPLATE_ID.test(templateId))
            throw new Error('templateId must look like k7.my-design');
          ed.tpl.id = templateId;
        }
        const name = readStr(args, "name");
        if (name !== undefined) ed.tpl.name = name;
        if (args.backgrounds !== undefined) {
          if (
            !Array.isArray(args.backgrounds) ||
            args.backgrounds.length < 1 ||
            args.backgrounds.length > CANVAS_MAX
          )
            throw new Error(`backgrounds must be one to ${CANVAS_MAX} colours`);
          const backgrounds = args.backgrounds.map((c, i) => {
            if (typeof c !== "string" || !HEX.test(c))
              throw new Error(`backgrounds[${i}] must be a colour like #909090`);
            return c.toLowerCase();
          });
          ed.setCanvas(backgrounds, ed.bgi);
        }
        const activeBackground = readNum(args, "activeBackground", 0);
        if (activeBackground !== undefined) {
          if (!Number.isInteger(activeBackground))
            throw new Error("activeBackground must be an integer");
          if (activeBackground >= ed.bgs.length)
            throw new Error(`activeBackground must be below ${ed.bgs.length}`);
          ed.selectBg(activeBackground);
        }
        const background = readHex(args, "background");
        if (background) ed.setBg(background);
        const maskColor = readHex(args, "maskColor");
        const jcardBackground = readHex(args, "jcardBackground");
        const maskRadius = readNum(args, "maskRadius", 0);
        if (maskRadius !== undefined && ed.surface !== "label")
          throw new Error(
            "maskRadius is the label's paper; the J-card's sheet has no corner radius",
          );
        if (maskColor !== undefined) ed.setMaskField("color", maskColor);
        if (maskRadius !== undefined) ed.setMaskField("radius", Math.round(maskRadius));
        if (jcardBackground !== undefined) ed.setJCardBackground(jcardBackground);
        if (args.fonts !== undefined) {
          if (typeof args.fonts !== "object" || args.fonts === null)
            throw new Error("fonts must be an object keyed by role");
          const fonts = args.fonts as Record<string, unknown>;
          for (const key of Object.keys(fonts)) {
            if (!FROLES.includes(key as (typeof FROLES)[number]))
              throw new Error(`fonts may only name ${FROLES.join(", ")}`);
            const entry = fonts[key];
            if (typeof entry !== "object" || entry === null)
              throw new Error(`fonts.${key} must be an object with face and size`);
            const face = readStr(entry as Args, "face");
            const size = readNum(entry as Args, "size", 1);
            const role = key as (typeof FROLES)[number];
            const f = ed.tpl.fonts[role];
            if (face !== undefined) {
              if (!sheetFaces.includes(face))
                throw new Error(
                  `face "${face}" is not one the sheet carries (${SHEET_FONTS.map(
                    ([f2, p2]) => `${f2} ${p2}`,
                  ).join(", ")})`,
                );
              f.ff = face;
            }
            if (size !== undefined) {
              if (!onSheet(f.ff, size))
                throw new Error(
                  `${f.ff} does not carry ${size} px; it carries ${sheetSizes(f.ff).join(", ")}`,
                );
              f.px = size;
            }
          }
        }
        if (args.jcardFonts !== undefined) {
          if (typeof args.jcardFonts !== "object" || args.jcardFonts === null)
            throw new Error("jcardFonts must be an object keyed by role");
          const fonts = args.jcardFonts as Record<string, unknown>;
          const patches: Array<{
            role: (typeof FROLES)[number];
            patch: { ff?: string; size?: number };
          }> = [];
          for (const key of Object.keys(fonts)) {
            if (!FROLES.includes(key as (typeof FROLES)[number]))
              throw new Error(`jcardFonts may only name ${FROLES.join(", ")}`);
            const entry = fonts[key];
            if (typeof entry !== "object" || entry === null)
              throw new Error(`jcardFonts.${key} must be an object with face and size`);
            const face = readStr(entry as Args, "face");
            const size = readNum(entry as Args, "size", 4, 640);
            const role = key as (typeof FROLES)[number];
            const patch: { ff?: string; size?: number } = {};
            if (face !== undefined) {
              if (!JCARD_FACES.includes(face))
                throw new Error(
                  `face "${face}" is not one the J-card wears (${JCARD_FACES.join(", ")})`,
                );
              patch.ff = face;
            }
            if (size !== undefined) {
              if (!Number.isInteger(size))
                throw new Error(`jcardFonts.${key}.size must be a whole number of units`);
              patch.size = size;
            }
            patches.push({ role, patch });
          }
          // Validate the whole object before applying any of it.
          for (const { role, patch } of patches) ed.setJCardFont(role, patch);
        }
        ed.commit(false);
        ed.emit();
        return {
          ok: true,
          message: "Updated the design.",
          design: designSummary(ed),
        };
      },
    },

    {
      name: "k7art-set-field-value",
      description:
        "Set what a field prints in the editor and in Preview — its simulated value, shown until the resolver sends a real one. On the label the field is a Slot; on the J-card it is title, artist or meta. An empty string restores the default.",
      inputSchema: schema(
        {
          surface: str(
            "Which surface's field to set. Defaults to the active surface.",
            { enum: SURFACES },
          ),
          field: str(
            `The field id. The label's: ${SLOTS.map((s) => s.id).join(", ")}. The J-card's: ${JCARD_FIELDS.join(", ")}.`,
            { enum: [...SLOTS.map((s) => s.id), ...JCARD_FIELDS] },
          ),
          value: str("The value to preview; empty restores the default."),
        },
        ["field", "value"],
      ),
      execute: (args) => {
        const surface = readSurface(ed, args);
        const field = readField(args, surface);
        if (!field) throw new Error("field is required");
        const value = readStr(args, "value") ?? "";
        ed.setSim(field, value);
        return {
          ok: true,
          message: value
            ? `${surface === "jcard" ? "J-card field" : "Field"} ${field} now previews "${value}".`
            : `${surface === "jcard" ? "J-card field" : "Field"} ${field} is back to its default.`,
          surface,
          field,
          value: surface === "jcard" ? ed.jcardSim(field as JCardField) : ed.simOf(field),
        };
      },
    },

    {
      name: "k7art-import-template",
      description:
        "Open a K7Art design in a new document. Accepts the template object or its JSON text. A design the packer would refuse is refused here with the reason, and leaves no half-open document.",
      inputSchema: schema(
        {
          template: {
            type: "object",
            description: "The K7Art object (or pass JSON text in this field).",
          },
          name: str("The document's tab name; defaults to the template's name."),
        },
        ["template"],
      ),
      execute: (args) => {
        const raw = args.template;
        if (raw === undefined)
          throw new Error("template is required: a K7Art object or its JSON text");
        const text = typeof raw === "string" ? raw : JSON.stringify(raw);
        const back = ed.active;
        ed.newDoc(undefined, readStr(args, "name"));
        try {
          ed.importTemplate(text);
        } catch (err) {
          ed.closeDoc(ed.active);
          if (back !== ed.active) ed.switchDoc(back);
          throw new Error(`template refused: ${(err as Error)?.message || err}`);
        }
        const r = ed.buildTemplate();
        return {
          ok: true,
          message: `Imported ${r.doc.id}.`,
          document: ed.doc?.name ?? null,
          templateId: r.doc.id,
          layers: ed.objs.length,
          errors: r.errs,
        };
      },
    },

    {
      name: "k7art-export-template",
      description:
        "Return the active design (or a named one) as a K7Art object, together with the validation errors that would stop it being exported.",
      inputSchema: schema({
        document: str(
          "The tab name or template id to export; defaults to the active design.",
        ),
      }),
      execute: (args) => {
        const wanted = readStr(args, "document");
        const back = ed.active;
        let index = back;
        if (wanted !== undefined) {
          index = ed.docs.findIndex(
            (d) => d.name === wanted || d.data.tpl?.id === wanted,
          );
          if (index < 0)
            throw new Error(
              `no document "${wanted}"; open documents are ${ed.docs.map((d) => d.name).join(", ")}`,
            );
        }
        if (index !== back) ed.switchDoc(index);
        const r = ed.buildTemplate();
        const docName = ed.doc?.name ?? null;
        if (index !== back) ed.switchDoc(back);
        return {
          ok: r.errs.length === 0,
          message:
            r.errs.length === 0
              ? `${r.doc.id} is valid k7art v1.`
              : `${r.errs.length} issue(s) would stop the export.`,
          document: docName,
          template: r.doc,
          errors: r.errs,
        };
      },
    },

    {
      name: "k7art-publish-pack",
      description:
        "Build a .k7artpack from named designs (all open designs by default): the version-4 label sheet, the designs' J-cards and the 128-byte header with the CRC. Returns the pack as base64 with its header facts, or the packer's refusals; it does not download a file or call the device.",
      inputSchema: schema(
        {
          designs: list(
            "The tab names or template ids to include; omit for every open design.",
            { type: "string" },
          ),
          packId: str(
            "The pack's slug id: lowercase letters, digits and dashes, up to 16 characters. Defaults to the active design's slug.",
          ),
          name: str("The pack's name for the App's list. Defaults to the active design's name."),
          author: str("The pack's author. Defaults to K7Art."),
        },
        [],
      ),
      execute: (args) => {
        const wanted = readStrings(args, "designs");
        let indices: number[];
        if (wanted) {
          indices = wanted.map((w) => {
            const i = ed.docs.findIndex(
              (d) => d.name === w || d.data.tpl?.id === w,
            );
            if (i < 0)
              throw new Error(
                `no document "${w}"; open documents are ${ed.docs.map((d) => d.name).join(", ")}`,
              );
            return i;
          });
        } else indices = ed.docs.map((_, i) => i);
        const built = ed.buildSelected(indices);
        if (built.errs.length)
          return {
            ok: false,
            message: `${built.errs.length} issue(s) would stop the pack.`,
            errors: built.errs,
          };
        const meta = {
          packId:
            readStr(args, "packId") ?? (slugOf(ed.tpl.id).slice(0, 16) || "k7art"),
          name: readStr(args, "name") ?? ed.tpl.name,
          author: readStr(args, "author") ?? "K7Art",
        };
        try {
          const pack = buildArtPack(
            [...built.built].sort((a, b) => a.i - b.i).map((b) => b.doc),
            meta,
          );
          return {
            ok: true,
            message: `${pack.designCount} design(s) packed as ${pack.filename}.`,
            filename: pack.filename,
            size: pack.totalSize,
            crc32: pack.crc,
            designCount: pack.designCount,
            designIds: pack.designIds,
            sheetVersion: pack.sheetVersion,
            fontCount: pack.fontCount,
            header: {
              magic: "K7ART",
              version: 1,
              headerSize: 128,
              totalSize: pack.totalSize,
              crc32: pack.crc,
              designCount: pack.designCount,
              sheetVersion: pack.sheetVersion,
              fontCount: pack.fontCount,
              sheetSize: pack.sheet.length,
              jcardsSize: pack.jcards.length,
              packId: meta.packId,
              name: meta.name,
              author: meta.author,
            },
            base64: toBase64(pack.bytes),
          };
        } catch (err) {
          if (err instanceof PackError)
            return {
              ok: false,
              message: `${err.errors.length} issue(s) would stop the pack.`,
              errors: err.errors,
            };
          throw err;
        }
      },
    },

    {
      name: "k7art-validate",
      description:
        "Check every open design and return the packer's reasons for any that would not export.",
      inputSchema: schema({}),
      execute: () => {
        const docs = ed.validateAll();
        const bad = docs.filter((d) => d.errs.length);
        return {
          ok: bad.length === 0,
          message: bad.length
            ? `${bad.length} of ${docs.length} design(s) would not export.`
            : `All ${docs.length} design(s) are valid.`,
          documents: docs,
        };
      },
    },

    {
      name: "k7art-document",
      description:
        "Work with the open documents: create a new one, switch to one, rename the active one, or close one. Closing the last document leaves an empty one open.",
      inputSchema: schema(
        {
          action: str("What to do.", {
            enum: ["new", "switch", "rename", "close"],
          }),
          name: str("A name, for new or rename."),
          index: num("The document's position in the tab strip, for switch or close.", {
            minimum: 0,
          }),
        },
        ["action"],
      ),
      execute: (args) => {
        const action = readEnum(args, "action", [
          "new",
          "switch",
          "rename",
          "close",
        ] as const)!;
        const name = readStr(args, "name");
        const index = readNum(args, "index", 0);
        if (action === "new") {
          ed.newDoc(undefined, name);
        } else if (action === "switch") {
          let i = index;
          if (i === undefined && name !== undefined) {
            i = ed.docs.findIndex(
              (d) => d.name === name || d.data.tpl?.id === name,
            );
            if (i < 0) throw new Error(`no document "${name}"`);
          }
          if (i === undefined)
            throw new Error("switch needs an index or a name");
          if (!ed.docs[i]) throw new Error(`no document at index ${i}`);
          ed.switchDoc(i);
        } else if (action === "rename") {
          if (!name?.trim()) throw new Error("rename needs a name");
          ed.renameDoc(ed.active, name);
        } else {
          const i = index ?? ed.active;
          if (!ed.docs[i]) throw new Error(`no document at index ${i}`);
          ed.closeDoc(i);
        }
        return {
          ok: true,
          message: `Document ${action}.`,
          documents: docsSummary(ed),
          active: ed.doc?.name ?? null,
        };
      },
    },

    {
      name: "k7art-history",
      description: "Undo or redo the last edit of the active design.",
      inputSchema: schema(
        {
          action: str("Undo or redo.", { enum: ["undo", "redo"] }),
        },
        ["action"],
      ),
      execute: (args) => {
        const action = readEnum(args, "action", ["undo", "redo"] as const)!;
        if (action === "undo") ed.undoAct();
        else ed.redoAct();
        return {
          ok: true,
          message: `${action === "undo" ? "Undid" : "Redid"} the last edit.`,
          canUndo: ed.undo.length > 0,
          canRedo: ed.redo.length > 0,
          layers: ed.objs.length,
        };
      },
    },
  ];
}
