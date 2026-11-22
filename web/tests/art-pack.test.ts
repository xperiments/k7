import { describe, expect, it } from 'vitest';

import {
  ArtPackError,
  PACK_TOTAL_MAX,
  artPackKey,
  crc32,
  readArtPack,
} from '../src/jcard/pack';
import { buildPackBytes, seedDesigns } from './fixtures/art-pack';

/**
 * The `.k7artpack` as the App reads it (`art-packs.md` §1): the header's fields and bounds,
 * the CRC over the payload, the sheet's own open and the J-cards. Every refusal names the
 * part that failed, and nothing half-read is ever returned.
 */

function refusal(bytes: Uint8Array): ArtPackError {
  try {
    readArtPack(bytes);
  } catch (error) {
    expect(error).toBeInstanceOf(ArtPackError);
    return error as ArtPackError;
  }
  throw new Error('the pack was accepted, but the test expected a refusal');
}

describe('the pack header', () => {
  it('reads the header, the sheet and the J-cards of a valid pack', () => {
    const designs = seedDesigns(2);
    const bytes = buildPackBytes({
      designs,
      packId: 'neon-2026',
      name: 'Neon 2026',
      author: 'Mara Vidal',
    });
    const pack = readArtPack(bytes);

    expect(pack.packId).toBe('neon-2026');
    expect(pack.name).toBe('Neon 2026');
    expect(pack.author).toBe('Mara Vidal');
    expect(pack.designCount).toBe(2);
    expect(pack.sheetVersion).toBe(5);
    expect(pack.fontCount).toBe(3);
    expect(pack.size).toBe(bytes.length);
    expect(pack.packCrc).toBe(crc32(bytes.subarray(128)));
    expect(pack.sheetCrc).toBe(new DataView(bytes.buffer).getUint32(128 + 16, true));
    expect(pack.designs.map((design) => design.id)).toEqual(designs.map((design) => design.id));
    expect(pack.jcards).toMatchObject({ v: 2 });
    expect(pack.designs[0]?.jcard.background).toMatch(/^#[0-9a-f]{6}$/i);
  });

  it('refuses a file shorter than the header', () => {
    expect(refusal(new Uint8Array(12)).part).toBe('header');
    expect(refusal(new Uint8Array(12)).message).toMatch(/shorter than its 128-byte header/);
  });

  it('refuses a file over the partition before reading any of it', () => {
    const error = refusal(new Uint8Array(PACK_TOTAL_MAX + 1));
    expect(error.part).toBe('bounds');
    expect(error.message).toMatch(/art partition/);
  });

  it('refuses another magic, another version and another header size', () => {
    expect(refusal(buildPackBytes({ packMagic: 'K7LBL\0\0\0' })).part).toBe('header');
    expect(refusal(buildPackBytes({ packVersion: 2 })).message).toMatch(/not version 1/);
    expect(refusal(buildPackBytes({ headerSize: 64 })).message).toMatch(/header size is not 128/);
  });

  it('refuses a total that is not the file, and a total that is not the parts', () => {
    const bytes = buildPackBytes();
    const wrongTotal = bytes.slice();
    new DataView(wrongTotal.buffer).setUint32(12, bytes.length + 1, true);
    expect(refusal(wrongTotal).message).toMatch(/the file is/);

    // The J-cards' offset moved under a header whose total stayed put. The checksum is
    // checked later, so this is the offset rule and not a checksum refusal.
    const wrongParts = buildPackBytes();
    new DataView(wrongParts.buffer).setUint32(36, 128, true);
    expect(refusal(wrongParts).part).toBe('header');
    expect(refusal(wrongParts).message).toMatch(/J-cards are not at their offset/);
  });

  it('refuses a pack with no designs, or more than 32', () => {
    expect(refusal(buildPackBytes({ designs: [], headerDesignCount: 0 })).part).toBe('bounds');
    const many = seedDesigns(33);
    expect(
      refusal(buildPackBytes({ designs: many, headerDesignCount: 33, sheetDesignCount: 33 })).message,
    ).toMatch(/1 to 32/);
  });

  it('refuses a sheet version or font count the compatibility contract forbids', () => {
    const old = refusal(buildPackBytes({ headerSheetVersion: 4 }));
    expect(old.part).toBe('header');
    expect(old.message).toBe(
      "The pack's label sheet is from an older editor (version 4); publish the designs again from /k7art.",
    );
    expect(refusal(buildPackBytes({ headerSheetVersion: 3 })).message).toMatch(
      /label sheet is version 3, not 5/,
    );
    expect(refusal(buildPackBytes({ fontCount: 0 })).message).toMatch(/font count/);
  });

  it('refuses a pack whose checksum disagrees with its payload', () => {
    expect(refusal(buildPackBytes({ corruptPackCrc: true })).part).toBe('checksum');
  });

  it('refuses a pack id that is not a slug', () => {
    expect(refusal(buildPackBytes({ packId: 'Not A Slug' })).part).toBe('header');
    expect(refusal(buildPackBytes({ packId: 'Neon_2026' })).message).toMatch(/slug/);
  });
});

describe('the label sheet', () => {
  it('refuses a sheet that is not the packer’s', () => {
    expect(refusal(buildPackBytes({ sheetMagic: 'NOTLBL\0\0' })).part).toBe('sheet');
    expect(refusal(buildPackBytes({ sheetVersion: 3 })).message).toMatch(
      /label sheet is version 3, not 5/,
    );
    const oldSheet = refusal(buildPackBytes({ sheetVersion: 4 }));
    expect(oldSheet.part).toBe('sheet');
    expect(oldSheet.message).toMatch(/older editor \(version 4\)/);
    expect(refusal(buildPackBytes({ sheetSpace: { w: 700, h: 359 } })).message).toMatch(
      /748x359/,
    );
  });

  it('refuses a sheet whose template count disagrees with the pack', () => {
    const error = refusal(buildPackBytes({ headerDesignCount: 2, sheetDesignCount: 1 }));
    expect(error.part).toBe('sheet');
    expect(error.message).toMatch(/says 2 designs; its label sheet carries 1/);
  });

  it('refuses a sheet with a broken checksum', () => {
    expect(refusal(buildPackBytes({ corruptSheetCrc: true })).part).toBe('sheet');
  });
});

describe('the J-cards', () => {
  it('refuses a J-cards document whose count disagrees with the pack', () => {
    const error = refusal(buildPackBytes({ headerDesignCount: 2, jcardsDesignCount: 1 }));
    expect(error.part).toBe('jcards');
    expect(error.message).toMatch(/J-cards carry 1/);
  });

  it('refuses bytes that are not JSON, and a document that is not version 2', () => {
    expect(refusal(buildPackBytes({ jcardsRaw: '{' })).part).toBe('jcards');
    const old = refusal(buildPackBytes({ jcardsOverride: { v: 1, designs: [] } }));
    expect(old.part).toBe('jcards');
    expect(old.message).toBe('The J-cards are not a version 2 { designs } document.');
  });

  it('refuses designs that are not in id order', () => {
    const designs = seedDesigns(2);
    const reversed = {
      v: 2,
      designs: [designs[1], designs[0]],
    };
    const error = refusal(buildPackBytes({ designs, jcardsOverride: reversed }));
    expect(error.part).toBe('jcards');
    expect(error.message).toMatch(/id order/);
  });

  it('refuses a design the renderer cannot draw', () => {
    const designs = seedDesigns(1);
    const broken = {
      v: 2,
      designs: [{ ...designs[0], jcard: { ...designs[0]!.jcard, parts: {} } }],
    };
    const error = refusal(buildPackBytes({ designs, jcardsOverride: broken }));
    expect(error.part).toBe('jcards');
    expect(error.message).toMatch(/not a J-card this App can draw/);
  });

  it('refuses a J-card over its 16 KB bound', () => {
    const error = refusal(buildPackBytes({ oversizedJcard: true }));
    expect(error.part).toBe('bounds');
    expect(error.message).toMatch(/over the 16384/);
  });
});

describe('the pack key', () => {
  it('is the CRC and the size, so two packs cannot share a cache record', () => {
    expect(artPackKey(0xabcd, 4096)).toBe('abcd:4096');
    expect(artPackKey(0xabcd, 4097)).not.toBe(artPackKey(0xabcd, 4096));
    expect(artPackKey(0xabce, 4096)).not.toBe(artPackKey(0xabcd, 4096));
  });

  it('checksums the format’s own test vector', () => {
    expect(crc32(new TextEncoder().encode('123456789'))).toBe(0xcbf43926);
  });
});
