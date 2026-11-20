# A pack is art in flash, and the factory and user designs are one pool

The K7's label sheet is packed at build and embedded in the app
(`components/app/screen_display/CMakeLists.txt`); the device opens it and `label_pick` chooses
one template by the track's `ink_seed`. There is no way for a user to add a design. The request
is a way to publish own **K7Art packs** from the editor and install them on the K7.

The constraints: 16 MB of flash, app 4 MB, Snapshot 2 MB at `0x410000`, about 9.9 MB free; the
device never parses JSON (ADR 0071) and a K7Art is JSON; the global premise forbids Track
uploads and SD content processing, so a pack cannot be a Track and should not need the SD; the
App owns the device link - LNA, Chrome only (ADR 0067); the editor is the Site's static
`/k7art` page (ADR 0112), so a pack's label sheet must be built in the browser; and the user
wants five factory designs plus five user designs to mean ten in the rotation, not a
replacement.

## Decision

- **A pack is a `.k7artpack`**: a header (magic, version, total size, CRC, design count, sheet
  version and font count, the sheet and J-card offsets and sizes, the name and author), the
  binary `label_sheet.bin` the packer's format defines (version 4), and the designs' `jcard`
  sections as JSON for the webapp. One file, one CRC over everything after the header.
- **The editor publishes it, the App installs it.** The editor builds the pack in the browser:
  a TypeScript port of `label_pack.py` writes the sheet, and it is checked byte for byte
  against the Python packer for every K7Art in `assets/k7art/`. The App's **Art packs** page
  imports the file, validates it, uploads it to the device's `art` endpoint, caches the
  J-cards, and can reset to the factory. The editor gets no device client; the App keeps the
  one LNA path.
- **One active user pack, additive to the factory.** The pack lives in a new `art` data
  partition (about 1 MB after the Snapshot); the factory sheet stays embedded in the app. The
  user's pack does not replace it: the K7's picker and the App's cover picker draw from
  **both sheets, factory first, each in id order**, so five plus five is ten. An empty, absent
  or invalid user pack leaves the factory alone.
- **The device validates and reads in place, and never parses the JSON.** The user sheet goes
  through `label_open`'s checks (magic, version, space, counts, CRC, font count) and is read in
  place; the K7Art JSON is only stored and served. Two `label_sheet_t`s are open while the
  pose draws; the picker maps `ink_seed % (factory + user)` to a sheet and a template index.
  `label_pick` itself does not change.
- **The pack is served read-only to the App.** `GET /api/art` reports the active pack (name,
  author, count, CRC, validity); `POST /api/art` writes and validates a pack; `GET
  /api/art/pack` returns its bytes for the App's J-cards; `DELETE /api/art` clears it. A pack
  is art, not a Track: no SD write, no audio, no Track upload. The global premise stands, and
  this ADR records the new art-in-flash capability.
- **The App's covers come from the pool.** A real cover picture still wins. When there is none,
  the face is the active pool's J-card - cover part on the case front and album card, spine
  part on the shelf's spine - chosen by the album's stable name hash. The five CSS faces
  retire.
- **The factory is a pack too, built at build time.** The repo's `assets/k7art/` files are
  packed into the embedded factory sheet by the Python packer and bundled into the webapp as
  the factory J-cards. The user's pack uses the same K7Art format and the same J-card sections.

## Considered options

- **Replace the factory sheet with the user pack.** Simplest storage and one sheet, but the
  device's labels vanish on a reset or a corrupt pack, and the request is for both sets to be
  selectable.
- **Several user packs, one active.** More flexible, but the device needs a pack table,
  selection state and eviction rules; the additive single pack already gives the pool.
- **The editor uploads directly to the K7.** One step for the designer, but a second device
  client, a second LNA permission and the firmware's CORS for the editor's origin; the App
  already owns the device link.
- **Run the Python packer at the Site's build for the user's designs.** No browser port, but
  the designs are the user's and never on the build machine; the pack must be built where the
  design is.
- **A J-card-only pack for the App.** It gives the webapp covers, but the request is for the K7
  to show the user's labels too.
- **Serve the pack from the SD card.** It needs a new host-written SD file and a show-path
  read, against the premise's SD-write list and the pose's no-read discipline; flash with the
  factory fallback is the smaller change.
- **Parse the K7Art JSON on the device.** It is the format the editor writes, but ADR 0071's
  reason for the binary sheet is the show path; the device stays on binary sheets.

## Consequences

- **The partition table gains `art`** (1 MB at `0x610000`); the app still embeds the factory
  sheet; the boot opens the factory sheet and the user sheet when it is valid; the pose's label
  is built from whichever the seed picks. A user pack survives a firmware flash that does not
  erase it; a full erase returns the factory.
- **The HTTP server gains four art routes** and the App's device protocol four methods. A
  partial upload fails the CRC and the boot falls back to the factory. The pack is bounded by
  the partition and by `label_open`'s caps (32 designs, 48 prims and 16 slots per sheet).
- **The App gains an Art packs page**, the import/upload/reset, a cache for the active pack's
  J-cards, and the pool-aware cover renderer. Its covers no longer have five CSS variants; the
  look's K19 records the change.
- **The browser packer is a second implementation of the sheet format.** The Python packer
  stays the build's; the TypeScript one is the editor's publish. CI compares their bytes for
  every K7Art in `assets/k7art/`; a disagreement fails the build. The sheet version, font
  count and vocabulary are the compatibility surface: a pack from another firmware's
  vocabulary or fonts is refused by name.
- **A pack is art in flash, not the SD card, and never a Track.** The global premise is
  untouched: the device serves no Track, accepts no Track and processes no SD content; the new
  capability is a bounded, CRC-checked art blob it reads in place and serves read-only.
- **The docs change in the same commit:** [`art-packs.md`](../specs/art-packs.md),
  [`k7art.md`](../specs/k7art.md), the label sheet's
  [`label-templates.md`](../specs/label-templates.md), the front door's and the stream
  server's art routes, [`k7-app.md`](../specs/k7-app.md), the look's K19, the glossary and the
  editor's README and skill. The Kanvas tasks carry the code.
