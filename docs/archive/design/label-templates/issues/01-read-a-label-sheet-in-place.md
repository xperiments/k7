# 01 — Read a label sheet in place

**What to build:** `components/portable/label/` — the reader for a **Label sheet**, with no LVGL
include and no target conditionals, so `host/Makefile` compiles it and `t32_label` can pin it.
The byte layout is [`docs/specs/label-templates.md`](../../../../specs/label-templates.md) §3.

The sheet is read in place: `label_open()` takes a pointer and a length, validates the header,
the counts, the CRC and every offset, and hands back a handle that is a view onto those bytes.
Nothing is copied, nothing is allocated except the handle itself (the Snapshot's shape, and the
reason it costs 0 B of PSRAM).

```c
typedef struct label_sheet label_sheet_t;

label_result_t label_open(const uint8_t *blob, size_t size, label_sheet_t **out);
label_result_t label_open_why(const uint8_t *blob, size_t size, label_sheet_t **out,
                              char *why, size_t why_size);
void label_close(label_sheet_t *s);

uint16_t label_templates(const label_sheet_t *s);
uint16_t label_pick(const label_sheet_t *s, uint32_t ink_seed);   /* seed % count */
const char *label_template_name(const label_sheet_t *s, uint16_t t);

uint16_t label_prim_count(const label_sheet_t *s, uint16_t t);
bool     label_prim_at(const label_sheet_t *s, uint16_t t, uint16_t i, label_prim_t *out);
const char *label_prim_text(const label_sheet_t *s, uint16_t t, uint16_t i);

uint16_t label_slot_count(const label_sheet_t *s, uint16_t t);
bool     label_slot_at(const label_sheet_t *s, uint16_t t, uint16_t i,
                       label_slot_t *out, const char **name);

label_inks_t label_inks(const label_sheet_t *s, uint16_t t);      /* paper, ink, rule, band[4] */
uint8_t      label_font(const label_sheet_t *s, uint16_t t, uint8_t role);

/* The design's `max`, honoured on a character boundary — never mid-UTF-8. */
void label_cut(const char *src, unsigned max_chars, char *dst, size_t cap);
```

`label_slot_t` carries `{ const char *name; uint8_t max; }`; an unknown name is not refused by
the reader (it is refused by the packer), it simply prints nothing.

**Blocked by:** none.

**Status:** done (host tier and the Sunton build; board not run)

- [x] `label_open` refuses each malformed case in §3.5 with a sentence in `why`, and never reads
      past `size`.
- [x] `label_pick` is `ink_seed % template_count`, and `label_templates` never returns 0 for a
      valid sheet.
- [x] A prim's text offset resolves inside the pool and is NUL-terminated.
- [x] `label_cut` cuts at `max` characters and keeps UTF-8 well formed; a shorter value is
      copied whole.
- [x] No allocation per accessor; the handle is the only malloc.
- [x] `host/Makefile` compiles the component and `layering.sh` (H3) sees it.

**Files:** `components/portable/label/include/label.h` (new),
`components/portable/label/label.c` (new), `components/portable/label/CMakeLists.txt` (new),
`host/tests/t32_label.c` (new), `host/Makefile`, `host/tests/layering.sh` if it needs the name.

**Tests:** `t32_label` builds sheets in memory — a good one and one per refusal — and asserts
every accessor, the pick rule, and `label_cut` on a multi-byte title.

**Source:** ADR 0071.

## Comments

- `components/portable/label/` holds `label.c` and `label.h`, with the component's
  `CMakeLists.txt` requiring `library` and `snapshot`. The sheet is read in place: `label_open`
  validates the header, the counts, the CRC and every offset, then allocates one small handle and
  nothing else.
- **`label_open` takes the font count.** A prim's font index can only be checked against the
  table the app actually links, so the caller passes `LABEL_FONT_COUNT` from the generated
  `label_fonts.h`. The host test passes its own.
- Offsets are pool-relative, which the spec now says and the verifier checks. `label_crc32` is
  this module's own bitwise CRC-32/IEEE - no dependency on `snapshot` for a checksum - and
  `t32_label` anchors it to the published `123456789` -> `0xCBF43926` vector.
- The reader refuses what the packer refuses: a prim outside 748x359, a text that is not ink, a
  rect carrying a text offset, a font index past the table, a slot `max` of 0 or over 96.
- `label_cut` cuts on a UTF-8 boundary and never splits the terminator; tested with two- and
  four-byte sequences and a one-byte buffer.
- **`t32_label` builds its own sheets byte by byte** with an independent CRC, so a change in the
  module cannot make the test agree with it: 106 checks, 19 of them refusals.
- Evidence: `make -C host check` green (unit + layering + HTTP acceptance), and
  `./tools/build.sh sunton build` links the component and the test's assertions hold on the host.
