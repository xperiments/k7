import { describe, expect, it } from "vitest";

import { makeZip } from "./files";

const bytes = (s: string) => new TextEncoder().encode(s);

const u32 = (b: Uint8Array, at: number) =>
  (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;

describe("makeZip", () => {
  it("writes one local header per entry and an end of central directory", async () => {
    const blob = makeZip([
      { name: "label.probe.json", bytes: bytes('{"v":1}') },
      { name: "probe.png", bytes: bytes("PNG") },
    ]);
    const b = new Uint8Array(await blob.arrayBuffer());
    expect(blob.type).toBe("application/zip");
    expect(u32(b, 0)).toBe(0x04034b50);
    const eocd = b.length - 22;
    expect(u32(b, eocd)).toBe(0x06054b50);
    expect(b[eocd + 8] | (b[eocd + 9] << 8)).toBe(2);
    const text = new TextDecoder().decode(b);
    expect(text).toContain("label.probe.json");
    expect(text).toContain("probe.png");
  });

  it("stores a file unmodified", async () => {
    const body = bytes("K7LBL");
    const b = new Uint8Array(await makeZip([{ name: "sheet.bin", bytes: body }]).arrayBuffer());
    expect(b.subarray(30 + "sheet.bin".length, 30 + "sheet.bin".length + body.length)).toEqual(body);
  });
});
