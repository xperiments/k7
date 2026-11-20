import { execFileSync } from "node:child_process";
import { mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { describe, expect, it } from "vitest";

import { crc32 } from "./crc";
import { buildTemplate, importTemplate } from "./tpl";
import {
  buildArtPack,
  buildLabelSheet,
  PACK_HEADER_SIZE,
  PACK_MAGIC,
  PackError,
  SHEET_VERSION,
  type PackDesign,
} from "./labelPack";

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, "../../../..");
const k7artDir = join(repo, "assets", "k7art");
const packer = join(repo, "tools", "label_pack.py");

const designs = (): PackDesign[] =>
  readdirSync(k7artDir)
    .filter((f) => f.endsWith(".json") && !f.startsWith("."))
    .sort()
    .map((f) => JSON.parse(readFileSync(join(k7artDir, f), "utf8")) as PackDesign);

const python = (() => {
  try {
    execFileSync("python3", ["--version"], { stdio: "ignore" });
    return "python3";
  } catch {
    return null;
  }
})();

describe("the browser packer", () => {
  it("writes the version-4 header the sheet's reader expects", () => {
    const sheet = buildLabelSheet(designs());
    const dv = new DataView(sheet.buffer, sheet.byteOffset);
    expect(new TextDecoder().decode(sheet.subarray(0, 8))).toBe("K7LBL\u0000\u0000\u0000");
    expect(dv.getUint16(8, true)).toBe(SHEET_VERSION);
    expect(dv.getUint16(10, true)).toBe(32);
    expect(dv.getUint32(12, true)).toBe(sheet.length);
    expect(dv.getUint32(16, true)).toBe(crc32(sheet.subarray(32)));
    expect(dv.getUint16(20, true)).toBe(748);
    expect(dv.getUint16(22, true)).toBe(359);
    expect(dv.getUint16(24, true)).toBe(designs().length);
  });

  it("writes a pack header whose CRC covers the sheet and the J-cards", () => {
    const pack = buildArtPack(designs(), {
      packId: "neon-2026",
      name: "Neon Skyline",
      author: "The Analog Sons",
    });
    const dv = new DataView(pack.bytes.buffer, pack.bytes.byteOffset);
    expect(pack.bytes.subarray(0, 8)).toEqual(PACK_MAGIC);
    expect(dv.getUint16(8, true)).toBe(1);
    expect(dv.getUint16(10, true)).toBe(PACK_HEADER_SIZE);
    expect(dv.getUint32(12, true)).toBe(pack.bytes.length);
    expect(dv.getUint32(16, true)).toBe(crc32(pack.bytes.subarray(PACK_HEADER_SIZE)));
    expect(dv.getUint16(20, true)).toBe(pack.designCount);
    expect(dv.getUint16(22, true)).toBe(SHEET_VERSION);
    expect(dv.getUint16(24, true)).toBe(pack.fontCount);
    expect(dv.getUint32(28, true)).toBe(PACK_HEADER_SIZE);
    expect(dv.getUint32(32, true)).toBe(pack.sheet.length);
    expect(dv.getUint32(36, true)).toBe(PACK_HEADER_SIZE + pack.sheet.length);
    expect(dv.getUint32(40, true)).toBe(pack.jcards.length);
    const text = (at: number, cap: number) =>
      new TextDecoder().decode(pack.bytes.subarray(at, at + cap)).replace(/\u0000+$/, "");
    expect(text(44, 16)).toBe("neon-2026");
    expect(text(60, 32)).toBe("Neon Skyline");
    expect(text(92, 32)).toBe("The Analog Sons");

    const jcards = JSON.parse(new TextDecoder().decode(pack.jcards)) as {
      v: number;
      designs: Array<{ id: string; name: string; inks: unknown; jcard: unknown }>;
    };
    expect(jcards.v).toBe(1);
    expect(jcards.designs.map((d) => d.id)).toEqual(pack.designIds);
    expect(jcards.designs[0].inks).toEqual((designs()[0] as { inks: unknown }).inks);
    expect(jcards.designs[0].jcard).toEqual((designs()[0] as { jcard: unknown }).jcard);
  });

  it("refuses no designs, a bad id and a header field over its cap", () => {
    expect(() => buildLabelSheet([])).toThrow(PackError);
    expect(() => buildArtPack([], { packId: "x", name: "x", author: "x" })).toThrow(
      "no designs",
    );
    expect(() =>
      buildArtPack(designs(), { packId: "Neon 2026", name: "x", author: "x" }),
    ).toThrow("pack id");
    expect(() =>
      buildArtPack(designs(), { packId: "neon", name: "n".repeat(33), author: "x" }),
    ).toThrow("over the 32");
  });

  it.skipIf(!python)(
    "writes byte-for-byte the sheet tools/label_pack.py writes",
    () => {
      const dir = mkdtempSync(join(tmpdir(), "k7art-sheet-"));
      try {
        execFileSync(python!, [packer, k7artDir, dir], { cwd: repo, stdio: "pipe" });
        const expected = readFileSync(join(dir, "label_sheet.bin"));
        const actual = Buffer.from(buildLabelSheet(designs()));
        expect(actual.equals(expected)).toBe(true);
      } finally {
        rmSync(dir, { recursive: true, force: true });
      }
    },
  );

  it("packs the editor's own read-back of a factory K7Art to the same bytes", () => {
    const file = readdirSync(k7artDir).find((f) => f.endsWith(".json"));
    expect(file).toBeTruthy();
    const text = readFileSync(join(k7artDir, file!), "utf8");
    const raw = JSON.parse(text) as PackDesign;
    const read = importTemplate(text, "#c7a867");
    const doc = buildTemplate(
      read.objs,
      read.mask,
      read.tpl,
      read.canvas,
      read.jcard,
    ).doc;
    expect(Buffer.from(buildLabelSheet([doc]))).toEqual(
      Buffer.from(buildLabelSheet([raw])),
    );
  });

  it.skipIf(!python)(
    "matches the Python packer over a two-design set, in id order",
    () => {
      const dir = mkdtempSync(join(tmpdir(), "k7art-sheet-two-"));
      try {
        const src = join(dir, "src");
        const out = join(dir, "out");
        mkdirSync(src);
        const base = designs()[0] as Record<string, unknown>;
        writeFileSync(
          join(src, "k7.zeta.json"),
          JSON.stringify({ ...base, id: "k7.zeta", name: "Zeta" }),
        );
        writeFileSync(
          join(src, "k7.alpha.json"),
          JSON.stringify({ ...base, id: "k7.alpha", name: "Alpha" }),
        );
        execFileSync(python!, [packer, src, out], { cwd: repo, stdio: "pipe" });
        const expected = readFileSync(join(out, "label_sheet.bin"));
        const two = [
          { ...base, id: "k7.alpha", name: "Alpha" },
          { ...base, id: "k7.zeta", name: "Zeta" },
        ] as PackDesign[];
        expect(Buffer.from(buildLabelSheet(two)).equals(expected)).toBe(true);
      } finally {
        rmSync(dir, { recursive: true, force: true });
      }
    },
  );
});
