# K7Art packs: the file, the App and the K7's art

A **K7Art pack** is a user's set of K7Art designs, published from the editor and installed on
the K7. The K7 shows its labels in the two-finger rotation and the App draws its J-cards on the
albums that have no picture. One pack is active at a time and it is **additive**: the factory
designs and the user's are one pool.

[ADR 0111](../adr/0111-a-pack-is-art-in-flash-and-the-factory-and-user-designs-are-one-pool.md)
records the decision; [ADR 0112](../adr/0112-the-k7art-editor-is-the-sites-k7art-page.md) the
editor's page; [ADR 0113](../adr/0113-a-k7arts-colours-are-literal.md) the literal colours that
make a v1 pack read no longer. The design file is [`k7art.md`](./k7art.md)'s, the sheet's binary is
[`label-templates.md`](./label-templates.md)'s, and the App's device link is
[`k7-app.md`](./k7-app.md)'s.

## 1. The pack file

A pack is one file, `*.k7artpack`, little-endian, its header 128 bytes and its payload the sheet
and the J-cards.

```text
header        128 B
label sheet   sheet_size B at sheet_offset   (the binary label_sheet.bin, version 5)
J-cards       jcards_size B at jcards_offset (UTF-8 JSON)
```

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 8 | `magic` | `K7ART`, NUL-padded to 8 - not the sheet's `K7LBL` |
| 8 | 2 | `version` | `1` |
| 10 | 2 | `header_size` | `128` |
| 12 | 4 | `total_size` | the file's whole length |
| 16 | 4 | `crc32` | CRC-32/IEEE over `[128, total_size)` |
| 20 | 2 | `design_count` | `1..32`, equal to the sheet's `template_count` |
| 22 | 2 | `sheet_version` | `5` |
| 24 | 2 | `font_count` | the firmware's `LABEL_FONT_COUNT` the sheet was built against |
| 26 | 2 | `reserved` | `0` |
| 28 | 4 | `sheet_offset` | `128` |
| 32 | 4 | `sheet_size` | the sheet's bytes |
| 36 | 4 | `jcards_offset` | `128 + sheet_size` |
| 40 | 4 | `jcards_size` | the J-cards' bytes |
| 44 | 16 | `pack_id` | a slug, UTF-8, NUL-padded - `neon-2026` |
| 60 | 32 | `name` | UTF-8, NUL-padded |
| 92 | 32 | `author` | UTF-8, NUL-padded |
| 124 | 4 | `reserved` | `0` |

- **The sheet is the packer's, unchanged.** It is the same `label_sheet.bin` the build embeds:
  magic `K7LBL`, version 5, its own CRC, the label sections of the pack's designs in id order,
  the font table compiled into the firmware. The pack's `sheet_version` and `font_count` are
  the compatibility contract: a pack whose sheet is another version, or built against another
  font table, is refused by name. **Version 5 is the literal colours**
  ([`label-templates.md`](./label-templates.md) §3): a pack published by an older editor has a
  version-4 sheet and is not read; the App says so and the designs are published again.
- **The J-cards are the designs' `jcard` sections**, in the same id order as the sheet:

  ```json
  { "v": 2,
    "designs": [
      { "id": "k7.neon", "name": "Neon Skyline", "jcard": { "space": { … }, "parts": { … },
        "background": "#F5F1E6", "fonts": { … }, "prims": [ … ] } }
    ] }
  ```

  The App renders them; the K7 never parses one. Every prim carries its own colour, so the
  J-card needs no design-wide inks; its own `background` is the sheet's colour.
- **The bounds**: `total_size` at most 1 MB (the `art` partition), `design_count` at most 32
  (the sheet's cap), each J-card at most 16 KB ([`k7art.md`](./k7art.md) §7). A pack with no
  designs is refused.
- **The CRC covers the payload**, so a truncated upload or a flipped byte is refused before
  anything is written to the device.

## 2. The factory pack

The repo's `assets/k7art/*.json` are the factory designs. At build, `label_pack.py` reads their
`label` sections and writes the embedded `label_sheet.bin` - the factory sheet, version 5. The
webapp's build reads the same files and bundles their `jcard` sections as the **factory
J-cards**: the default pool that every album without a picture draws from. The factory pack is not a `.k7artpack` on the device; it is the app's embedded sheet plus
the app's bundled J-cards.

## 3. The editor's publish

The editor is the Site's static page at `/k7art` ([ADR 0112](../adr/0112-the-k7art-editor-is-the-sites-k7art-page.md)).
Publishing is a browser build, not a request to a server.

- **Choose the designs.** Publish takes any subset of the open K7Art documents (at least one),
  a `pack_id`, a name and an author, and writes one `.k7artpack` as a download.
- **Build the sheet.** A TypeScript port of `label_pack.py` writes the binary sheet from the
  designs' `label` sections: the header, the template records, the canvas rows, the prims, the
  slots, the string pool and the CRC. Its refusals are the packer's, with the same names.
- **Byte parity is the seam.** CI runs the TypeScript writer and `label_pack.py` over every
  `assets/k7art/*.json` and compares the bytes; a disagreement fails the build. The Python
  packer stays the build's; the TypeScript one is the editor's.
- **Build the J-cards and the header**: each design's `jcard` under `v: 2`, the pack's header
  (the sheet's size and CRC, the design count, the sheet's version, the font count, the name,
  the author, the CRC) and the download. A design the editor would refuse at Export is refused
  here, by name, before the pack exists.
- **The editor does not install.** It has no device client; the App installs
  ([ADR 0111](../adr/0111-a-pack-is-art-in-flash-and-the-factory-and-user-designs-are-one-pool.md)).
  A published pack is a file the user takes to the App.

## 4. The App's install

The App's **Art packs** page (Settings) is the one place a pack is installed.

- **Import.** A file picker (or a drop) takes a `.k7artpack`; the App validates the header, the
  CRC, the sheet's own open (the magic, the version, the space, the counts), the J-cards' JSON
  and the bounds, and shows the designs' names and their J-card previews before anything is
  uploaded. A bad pack says which part failed and changes nothing.
- **Upload.** The App sends the pack to the device's `POST /api/art`, with progress and the
  device's refusal if it comes back. On success the device's status names the active pack; the
  App caches the pack's J-cards (IndexedDB, keyed by the pack's CRC and size) so the covers
  draw before the next fetch.
- **The active pack and the reset.** The page shows the factory designs' count, the user
  pack's name, author and count, and the pool's total; **Reset to factory** sends `DELETE
  /api/art` and drops the cached J-cards. The factory sheet is not touched.
- **The device only.** Installing needs the K7's art route to answer; when it does not — no
  player, or the card in the reader — the page says so and offers no upload. The App's covers
  still draw from the factory J-cards.
- **The link to the editor.** The page links to `/k7art` for a user who wants to make a pack.

The App's device protocol gains four methods - `art.status`, `art.upload`, `art.pack`,
`art.reset` - over the existing direct/local transports ([`front-door.md`](./front-door.md)).

## 5. The K7: the art partition and the picker

- **The pack lives in an `art` data partition**: 1 MB at `0x610000`, after the 4 MB app and the
  2 MB Snapshot (`partitions.csv`). It holds the pack's bytes; the factory sheet stays embedded
  in the app. A firmware flash that does not erase the partition leaves the user pack in place;
  a full erase restores the factory.
- **Two sheets, one pool.** The app opens the embedded factory sheet and, when the partition's
  header and CRC and the sheet's own checks pass, the user sheet. The pool is the factory sheet
  first, then the user sheet, each in id order; the factory's designs and the user pack's are
  one pool. The picker is `ink_seed % (factory_count + user_count)`; an index before
  `factory_count` selects the factory sheet and its template, the rest the user sheet and
  `index - factory_count`. `label_pick` still picks within a sheet; the mapping is the app's
  label code.
- **An absent or invalid pack is the factory.** A bad CRC, a bad sheet, another version or font
  count → the factory sheet alone; the status says why. The device never refuses to boot
  because of the user's pack.
- **A new pack is swapped in under the LVGL lock.** The upload writes the partition, validates
  it, opens the user sheet and replaces the pointer; the next label build (a Track change or
  the pose's entry) draws from the new pool. The pack is read-only after that.
- **The device reads the sheet in place and serves the pack read-only.** It never parses the
  K7Art JSON; `GET /api/art/pack` returns the pack's bytes, and the App parses the J-cards.

### 5.1 The endpoints

| Route | Does |
|---|---|
| `GET /api/art` | The status: `{ installed, valid, name, author, design_count, sheet_crc, factory_count, why }` - `why` names an invalid pack's reason |
| `POST /api/art` | The body is the pack; the device checks the header, the CRC, the bounds and the sheet, writes the partition, and returns the new status. `413` over 1 MB, `422` on any check, and nothing is written until the pack is whole |
| `GET /api/art/pack` | The active pack's bytes, for the App's J-cards; `404` when there is none |
| `DELETE /api/art` | Erases the partition and returns the factory status |

The routes are on the K7's existing front door, with the App's origin and the dev origin's
CORS and the same Local Network Access gate the rest of the API has (ADR 0067). The App is the
only writer; the editor has no device client.

## 6. The App's covers

The App's cover rule is unchanged at the top: **a real cover picture wins**. When there is
none, the face is the pool's J-card, chosen by the same stable hash that picks a face today
(`hueOf(title + artist)`), now modulo the pool's size.

- **The pool is the factory J-cards plus the active user pack's**, factory first. The App
  fetches the active pack's J-cards from `GET /api/art/pack` (or its cache) and keeps the
  factory's bundled; the factory designs are always there, so an album always has a face.
- **The cover part is the case front** (the shelf and the Player), the album card (square,
  centre-cropped, the safe square), and the mini case. **The spine part is the shelf's left
  face.** The flap is drawn nowhere yet.
- **The five CSS faces retire**; `web/src/ui/album-art.tsx` keeps the no-cover slot and the
  album's hue and loses the five drawings. The look's K19 records it
  ([`k7-look.md`](./k7-look.md) §5.3).
- **The renderer** is [`k7art.md`](./k7art.md) §5's: the prims in the DOM, each in its own
  colour, the faces and sizes, the rotation and the wrap. The J-card is decorative; the
  album's name is beside it.
- **A Track's case** (the Player, the Player bar) uses the same pool and the Track's
  `title`/`artist`/`album` as the three fields, as the CSS faces did; an album's case uses the
  album's name, its byline and the default meta.

## 7. The premise and limits

The K7 serves no Track and accepts no Track; it processes no SD content. A pack is **art**: it
is written to flash, read in place and served read-only, and it is never a Track. The SD card's
writes are the ones the premise already lists; this change adds none. The device never parses
a K7Art; the binary sheet is the only thing it reads.

- **One active user pack.** A second upload replaces the first; the factory is never replaced.
  The user's designs are one set, not a library with selection state.
- **The LAN is the trust boundary.** The art routes are on the App's existing gate; a pack is
  validated before it is written, and a failed write leaves the factory. A malicious pack is
  bounded by the partition, the caps, the CRC and `label_open`; it cannot execute, load a
  font, or name a file.
- **Power loss during an upload** leaves a header whose CRC does not match; the boot falls back
  to the factory. The next boot reports the invalid pack in `GET /api/art`.
- **The pack is not a backup.** Reset to factory is `DELETE /api/art`; the file the user
  published is their copy.

## 8. Tests and costs

- **The parity**: the TypeScript sheet writer against `label_pack.py` for every factory K7Art,
  byte for byte, in CI and in the editor's tests.
- **The pack's C**: the header's fields, the CRC, an over-size body, a truncated body, a bad
  sheet version and a bad font count; the factory fallback.
- **The device's two sheets**: the picker's mapping for factory + user counts, the factory-only
  fallback, the swap under the lock, the status's `why`.
- **The App's install**: a valid pack's import/upload/cache, a bad pack's refusal, the reset,
  the covers before and after, the card-in-reader mode.
- **The editor's publish**: the selection, the bounds, the refusals, the downloaded pack's
  header and J-cards.
- **The Site**: `/k7art` serves the editor and its `skill.md`, the base path's assets and a
  fresh boot.
- **The costs**: 1 MB of flash always reserved for the partition; a pack is a few KB to tens of
  KB (the sheet plus the J-cards); the upload is one request; the boot validates two sheets; the
  App bundles the factory J-cards and caches one user pack; the label's per-frame cost does not
  change, because one sheet's template is open at a time (ADR 0071's costs still hold).
