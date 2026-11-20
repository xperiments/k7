import type { Editor } from "@/engine/editor";
import type { Obj, Tool } from "@/engine/types";
import type { LabelTool } from "@/webmcp/types";
import { skillProse } from "./skill";
import {
  CANVAS_MAX,
  FROLES,
  PRINT_H,
  PRINT_W,
  ROLES,
  SCREEN_H,
  SCREEN_W,
  SHEET_FONTS,
  SLOTS,
  TEXT_ROLES,
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
const TEMPLATE_ID = /^label\.[a-z0-9][a-z0-9-]*$/;

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
    throw new Error("ids must be a non-empty array of layer ids from label-editor-read");
  return v as number[];
}

function layerById(ed: Editor, id: unknown): Obj {
  if (typeof id !== "number")
    throw new Error("id must be the number from label-editor-read");
  const o = ed.objs.find((x) => x.id === id);
  if (!o) throw new Error(`there is no layer ${id} in the active design`);
  return o;
}

/** A compact view of one layer, enough for an agent to reason and then edit it. */
function layerSummary(ed: Editor, o: Obj) {
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
    role: o.role ?? null,
    x: Math.round(o.x),
    y: Math.round(o.y),
    width: Math.round(o.w),
    height: o.type === "text" ? Math.round(ed.pxOf(o) * 1.2) : (o.h ?? 0),
    rotation: Math.round(o.rot || 0),
    locked: !!o.locked,
    insideMask: !!o.m,
    group: ed.groups.find((g) => g.id === o.g)?.name ?? null,
  };
}

function designSummary(ed: Editor) {
  return {
    templateId: ed.tpl.id,
    templateName: ed.tpl.name,
    background: ed.bg,
    backgrounds: ed.bgs,
    backgroundIndex: ed.bgi,
    inks: { ink: ed.tpl.inks.ink, rule: ed.tpl.inks.rule, band: ed.tpl.inks.band },
    paper: ed.mask ? { colour: ed.mask.color, radius: ed.mask.radius } : null,
    fonts: FROLES.map((role) => ({
      role,
      face: ed.tpl.fonts[role].ff,
      size: ed.tpl.fonts[role].px,
    })),
    layers: ed.objs.length,
    errors: ed.buildTemplate().errs,
  };
}

function docsSummary(ed: Editor) {
  return ed.docs.map((d, i) => ({
    name: d.name,
    templateId: d.data.tpl?.id ?? null,
    active: i === ed.active,
  }));
}

export function labelEditorTools(
  ed: Editor,
  opts: { guide: string },
): LabelTool[] {
  return [
    {
      name: "label-editor-read",
      description:
        "Read the label editor: the print's coordinate space, the fields, roles and type faces a design may use, the open documents, the active design's layers, mask, groups, selection and export errors.",
      inputSchema: schema({
        includeTemplate: bool(
          "Also return the active design as a label-template v1 object.",
        ),
      }),
      execute: (args) => {
        const tpl = ed.buildTemplate();
        return {
          ok: true,
          board: {
            print: { width: PRINT_W, height: PRINT_H },
            screen: { width: SCREEN_W, height: SCREEN_H },
          },
          fields: SLOTS.map((s) => ({ id: s.id, label: s.label })),
          roles: { shape: ROLES, text: TEXT_ROLES },
          typeRoles: FROLES.map((role) => ({
            role,
            face: ed.tpl.fonts[role].ff,
            size: ed.tpl.fonts[role].px,
            faces: sheetFaces,
            sizes: sheetSizes(ed.tpl.fonts[role].ff),
          })),
          documents: docsSummary(ed),
          active: {
            name: ed.doc?.name ?? null,
            templateId: ed.tpl.id,
            templateName: ed.tpl.name,
            background: ed.bg,
            backgrounds: ed.bgs,
            backgroundIndex: ed.bgi,
            layers: ed.objs.map((o) => layerSummary(ed, o)),
            mask: ed.mask
              ? { colour: ed.mask.color, radius: ed.mask.radius }
              : null,
            groups: ed.groups.map((g) => ({
              id: g.id,
              name: g.name,
              layers: ed.objs.filter((o) => o.g === g.id).map((o) => o.id),
            })),
            selection: ed.selIds,
            errors: tpl.errs,
          },
          ...(args.includeTemplate === true ? { template: tpl.doc } : {}),
        };
      },
    },

    {
      name: "label-editor-guide",
      description:
        "Read this once before designing: what a K7 label is for, how the print and the Card's window shape a layout, the fields and the three type roles, the workflow, what gets refused, and how to do all of it from the visible controls when the tools cannot be reached. The same guide is served at /skill.md.",
      inputSchema: schema({}),
      execute: () => ({
        ok: true,
        guide: skillProse(opts.guide),
        url: "/skill.md",
      }),
    },

    {
      name: "label-editor-add-layer",
      description:
        "Add a layer to the active design. Coordinates are design pixels in the 748x359 print, origin at its top-left; a layer may sit outside the print and still export, where the print crops it. A text is static unless a field is given, and starts in the caption type role; set fontRole for title or numeral.",
      inputSchema: schema(
        {
          type: str("What to add.", {
            enum: ["rect", "square", "circle", "text"],
            default: "rect",
          }),
          x: num("Left edge in design pixels. Default 40."),
          y: num("Top edge in design pixels. Default 40."),
          width: num("Width in design pixels; for a text this is its wrap width.", {
            minimum: 1,
          }),
          height: num("Height in design pixels, for a shape. A text's height follows its type.", {
            minimum: 1,
          }),
          text: str(
            "The string a static text prints. Do not combine with field.",
          ),
          field: str(
            `The field a dynamic text prints. Do not combine with text. One of: ${SLOTS.map((s) => s.id).join(", ")}.`,
            { enum: SLOTS.map((s) => s.id) },
          ),
          role: str(
            `The colour role. A shape takes ${ROLES.join(", ")}; a text takes ${TEXT_ROLES.join(", ")}.`,
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
        const type = (readEnum(args, "type", ["rect", "square", "circle", "text"] as const) ??
          "rect") as Tool;
        const x = Math.round(readNum(args, "x") ?? 40);
        const y = Math.round(readNum(args, "y") ?? 40);
        const text = readStr(args, "text");
        const field = readStr(args, "field");
        const w = readNum(args, "width", 1);
        const h = readNum(args, "height", 1);
        const role = readStr(args, "role");
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
        if (field !== undefined && !slotOf(field))
          throw new Error(
            `field "${field}" is not one of ${SLOTS.map((s) => s.id).join(", ")}`,
          );
        if (text !== undefined && !text.trim()) throw new Error("text must not be empty");
        if (h !== undefined && type === "text")
          throw new Error("a text's height follows its type; set width (the wrap width)");
        if (role !== undefined) {
          const allowed = type === "text" ? TEXT_ROLES : ROLES;
          if (!allowed.includes(role))
            throw new Error(
              `role for a ${type} must be one of ${allowed.join(", ")}`,
            );
        }
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
        }
        if (role !== undefined) o.role = role;
        if (fontRole !== undefined) o.fr = fontRole;
        if (align !== undefined) o.align = align;
        if (layerName !== undefined) o.label = layerName;
        if (rot !== undefined) o.rot = Math.round(((rot % 360) + 360) % 360);
        ed.commit(false);
        ed.emit();
        return {
          ok: true,
          message: `Added ${o.type} ${o.id}.`,
          id: o.id,
          layer: layerSummary(ed, o),
        };
      },
    },

    {
      name: "label-editor-update-layer",
      description:
        "Change one layer of the active design: position or size in design pixels, rotation, its text or field, colour role, font role, alignment, layer name, test value or lock. Only the fields given change.",
      inputSchema: schema(
        {
          id: num("The layer id from label-editor-read.", { minimum: 1 }),
          x: num("Left edge in design pixels."),
          y: num("Top edge in design pixels."),
          width: num("Width in design pixels; a text's wrap width.", { minimum: 1 }),
          height: num("Height in design pixels, for a shape.", { minimum: 1 }),
          rotation: num("Rotation in degrees, clockwise."),
          text: str("Make the text static and print this string."),
          field: str(
            `Make the text dynamic and print this field. One of: ${SLOTS.map((s) => s.id).join(", ")}.`,
            { enum: SLOTS.map((s) => s.id) },
          ),
          role: str("The colour role to print in."),
          fontRole: str("The text's type role.", { enum: FROLES }),
          align: str("The text's horizontal alignment.", {
            enum: ["left", "center", "right"],
          }),
          layerName: str("The layer's name in the Layers panel."),
          testValue: str("What a dynamic text previews in the editor and Preview."),
          locked: bool("Lock or unlock the layer. Other changes need it unlocked."),
        },
        ["id"],
      ),
      execute: (args) => {
        const o = layerById(ed, args.id);
        const locked = readBool(args, "locked");
        const x = readNum(args, "x");
        const y = readNum(args, "y");
        const w = readNum(args, "width", 1);
        const h = readNum(args, "height", 1);
        const rot = readNum(args, "rotation");
        const text = readStr(args, "text");
        const field = readStr(args, "field");
        const role = readStr(args, "role");
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
        if (field !== undefined && !slotOf(field))
          throw new Error(
            `field "${field}" is not one of ${SLOTS.map((s) => s.id).join(", ")}`,
          );
        if (text !== undefined && !text.trim()) throw new Error("text must not be empty");
        if (h !== undefined && o.type === "text")
          throw new Error("a text's height follows its type; set width (the wrap width)");
        if (role !== undefined) {
          const allowed = o.type === "text" ? TEXT_ROLES : ROLES;
          if (!allowed.includes(role))
            throw new Error(
              `role for a ${o.type} must be one of ${allowed.join(", ")}`,
            );
        }
        if (fontRole !== undefined && o.type !== "text")
          throw new Error("only a text has a font role");
        if (align !== undefined && o.type !== "text")
          throw new Error("only a text has an alignment");
        if (testValue !== undefined && o.type !== "text")
          throw new Error("only a text has a test value");

        if (locked !== undefined) o.locked = locked;
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
        }
        if (role !== undefined) o.role = role;
        if (fontRole !== undefined) o.fr = fontRole;
        if (align !== undefined) o.align = align;
        if (layerName !== undefined) o.label = layerName;
        if (testValue !== undefined) {
          if (testValue === "") delete o.sim;
          else o.sim = testValue;
        }
        ed.commit(false);
        ed.emit();
        return {
          ok: true,
          message: `Updated layer ${o.id}.`,
          layer: layerSummary(ed, o),
        };
      },
    },

    {
      name: "label-editor-delete-layer",
      description: "Delete one layer of the active design.",
      inputSchema: schema(
        { id: num("The layer id from label-editor-read.", { minimum: 1 }) },
        ["id"],
      ),
      execute: (args) => {
        const o = layerById(ed, args.id);
        if (o.locked) throw new Error(`layer ${o.id} is locked; unlock it first`);
        ed.selectLayers([o.id]);
        ed.delSel();
        return { ok: true, message: `Deleted layer ${o.id}.`, layers: ed.objs.length };
      },
    },

    {
      name: "label-editor-select-layer",
      description:
        "Select one or more layers, so the person at the screen sees the same selection the tools are working on.",
      inputSchema: schema(
        {
          ids: list(
            "Layer ids from label-editor-read. The last one is the primary selection.",
            { type: "number" },
          ),
        },
        ["ids"],
      ),
      execute: (args) => {
        const ids = readIds(args)!;
        const missing = ids.filter((id) => !ed.objs.some((o) => o.id === id));
        if (missing.length) throw new Error(`no layer ${missing.join(", ")} in the active design`);
        ed.selectLayers(ids);
        return { ok: true, selection: ed.selIds };
      },
    },

    {
      name: "label-editor-move-layer",
      description:
        "Move a layer through the stack: one step up or down, or straight to the top or bottom. The top of the stack prints last, over the layers below it.",
      inputSchema: schema(
        {
          id: num("The layer id from label-editor-read.", { minimum: 1 }),
          direction: str("Which way to move it.", {
            enum: ["up", "down", "top", "bottom"],
          }),
        },
        ["id", "direction"],
      ),
      execute: (args) => {
        const o = layerById(ed, args.id);
        const direction = readEnum(args, "direction", [
          "up",
          "down",
          "top",
          "bottom",
        ] as const)!;
        ed.selectLayers([o.id]);
        if (direction === "up" || direction === "down") ed.mv(direction === "up" ? 1 : -1);
        else {
          const b = ed.blockOf(o);
          if (direction === "top") ed.moveBlock(b.lo, b.hi, ed.objs.length);
          else ed.moveBlock(b.lo, b.hi, 0);
        }
        return {
          ok: true,
          message: `Moved layer ${o.id} ${direction}.`,
          index: ed.objs.findIndex((x) => x.id === o.id),
        };
      },
    },

    {
      name: "label-editor-group-layers",
      description:
        "Group layers into one layer group, or ungroup a group. Grouping needs the layers to be contiguous and on one side of the LayerMask, which the tool checks.",
      inputSchema: schema(
        {
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
        const action = readEnum(args, "action", ["group", "ungroup"] as const) ?? "group";
        const ids = readIds(args);
        if (ids) ed.selectLayers(ids);
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
            layers: ed.objs.filter((o) => o.g === g.id).map((o) => o.id),
          })),
          selection: ed.selIds,
        };
      },
    },

    {
      name: "label-editor-set-design",
      description:
        "Set the design's template id or name, its canvas backgrounds (the presets and the active one), its colourway (ink, rule, the eight bands, the paper), its paper radius, or one of its type roles' face and size. Every colour is a hex string; a face and size must be a pair the sheet carries.",
      inputSchema: schema({
        templateId: str("The template id, like label.my-design; it names the exported file."),
        name: str("The design's human name."),
        background: str("The active canvas background, as #rrggbb; it sets the active preset."),
        backgrounds: list(
          `The canvas background presets, as #rrggbb strings, in order: one to ${CANVAS_MAX}.`,
          { type: "string" },
        ),
        activeBackground: num("The index of the active canvas background preset.", {
          minimum: 0,
        }),
        ink: str("The colour of the ink role, as #rrggbb."),
        rule: str("The colour of the rule role, as #rrggbb."),
        bands: list("The eight band colours, as #rrggbb strings, in band order.", {
          type: "string",
        }),
        paper: str(
          "The paper's colour, as #rrggbb. Adds the LayerMask paper if the design has none.",
        ),
        paperRadius: num("The paper's corner radius in design pixels.", { minimum: 0 }),
        fonts: {
          type: "object",
          description:
            `Type roles to change: a face from ${sheetFaces.join(", ")} and one of that face's sizes.`,
          properties: Object.fromEntries(
            FROLES.map((role) => [
              role,
              {
                type: "object",
                description: `The ${role} role.`,
                properties: {
                  face: str("The face name.", { enum: sheetFaces }),
                  size: num("The pixel size the sheet carries for that face."),
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
            throw new Error('templateId must look like label.my-design');
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
        const ink = readHex(args, "ink");
        if (ink) ed.tpl.inks.ink = ink;
        const rule = readHex(args, "rule");
        if (rule) ed.tpl.inks.rule = rule;
        if (args.bands !== undefined) {
          if (!Array.isArray(args.bands) || args.bands.length !== 8)
            throw new Error("bands must be the eight band colours, in order");
          const bands = args.bands.map((b, i) => {
            if (typeof b !== "string" || !HEX.test(b))
              throw new Error(`bands[${i}] must be a colour like #c68b2e`);
            return b.toLowerCase();
          });
          ed.tpl.inks.band = bands;
        }
        const paper = readHex(args, "paper");
        const paperRadius = readNum(args, "paperRadius", 0);
        if (paper !== undefined || paperRadius !== undefined) {
          if (!ed.mask) ed.addMask();
          if (paper !== undefined) ed.mask!.color = paper;
          if (paperRadius !== undefined) ed.mask!.radius = Math.round(paperRadius);
        }
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
      name: "label-editor-set-field-value",
      description:
        "Set what a field prints in the editor and in Preview — its simulated value, shown until the resolver sends a real one. An empty string restores the field's default.",
      inputSchema: schema(
        {
          field: str(
            `The field id. One of: ${SLOTS.map((s) => s.id).join(", ")}.`,
            { enum: SLOTS.map((s) => s.id) },
          ),
          value: str("The value to preview; empty restores the default."),
        },
        ["field", "value"],
      ),
      execute: (args) => {
        const field = readStr(args, "field");
        if (!field || !slotOf(field))
          throw new Error(`field must be one of ${SLOTS.map((s) => s.id).join(", ")}`);
        const value = readStr(args, "value") ?? "";
        ed.setSim(field, value);
        return {
          ok: true,
          message: value
            ? `Field ${field} now previews "${value}".`
            : `Field ${field} is back to its default.`,
          field,
          value: ed.simOf(field),
        };
      },
    },

    {
      name: "label-editor-import-template",
      description:
        "Open a label-template v1 design in a new document. Accepts the template object or its JSON text. A design the packer would refuse is refused here with the reason, and leaves no half-open document.",
      inputSchema: schema(
        {
          template: {
            type: "object",
            description: "The label-template v1 object (or pass JSON text in this field).",
          },
          name: str("The document's tab name; defaults to the template's name."),
        },
        ["template"],
      ),
      execute: (args) => {
        const raw = args.template;
        if (raw === undefined)
          throw new Error("template is required: a label-template v1 object or its JSON text");
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
      name: "label-editor-export-template",
      description:
        "Return the active design (or a named one) as a label-template v1 object, together with the validation errors that would stop it being exported.",
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
              ? `${r.doc.id} is valid label-template v1.`
              : `${r.errs.length} issue(s) would stop the export.`,
          document: docName,
          template: r.doc,
          errors: r.errs,
        };
      },
    },

    {
      name: "label-editor-validate",
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
      name: "label-editor-document",
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
      name: "label-editor-history",
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
