# 04 — Fill the slots from the Snapshot

**What to build:** the values a design's slots print, in the portable component, with no LVGL and
no card access — [`docs/specs/label-templates.md`](../../../specs/label-templates.md) §5 is the
table.

```c
#define LABEL_VALUE_MAX 96
typedef struct { char text[LABEL_SLOT_COUNT][LABEL_VALUE_MAX + 1]; } label_values_t;

/* The row for `relpath`, if the library still has it and the Snapshot still matches it. */
void label_values_fill(label_values_t *v, const library_t *lib, const snapshot_t *snap,
                       const char *relpath);

/* "" for a name the vocabulary does not know, or an unknown value. */
const char *label_values_get(const label_values_t *v, const char *slot_name);
```

The lookup is RAM only: `library_find(relpath)` → `library_size()` → `snapshot_path_hash()` →
`snapshot_find()`. No entry, no row, or a row whose unknown marker is set → the slot is `""`.

Formatting, and the reason each is what it is:

- `bpm` — `bpm_x10 / 10`, rounded, never a decimal (`124`, not `124.0`): the editor's field holds
  three characters.
- `musical_key` — the Camelot code the MCP layer already prints (`1..12` + `A`, `13..24` + `B`), so the
  panel and `/api/status` say the same thing about the same Track.
- `energy`, `danceability`, `valence`, `arousal` — `0..100`, the image's own units.
- `mood` — `snapshot_mood_name()` of the lowest set bit, which is the image's order: happy, sad,
  aggressive, relaxed, party.
- Strings — the row's own, copied whole.

The design's `max` is not this module's: the resolver caps at `LABEL_VALUE_MAX` and the view
cuts with `label_cut()` (ticket 01) to the slot's own bound.

**Blocked by:** 01 — the component and its test binary exist there.

**Status:** done (host tier and the Sunton build)

- [x] A fixture library + Snapshot row fills all eleven slots; every formatting rule above is
      pinned by a test, including a `bpm_x10` that rounds up.
- [x] A Track with no row, a Track the library no longer has, and a size mismatch all leave the
      values empty rather than stale.
- [x] An unknown slot name returns `""` and does not read out of bounds.
- [x] No filesystem call, no allocation, no LVGL include; the component still passes `layering`.

**Files:** `components/portable/label/include/label_values.h` (new),
`components/portable/label/label_values.c` (new), `components/portable/label/CMakeLists.txt`,
`host/tests/t32_label.c`, `host/Makefile`, `host/fixtures/` if a fixture Library is wanted.

**Tests:** `t32_label` builds a small Library and a Snapshot image in memory — as `t29_snapshot`
and `t31_card_select` already do — and asserts each slot and each empty case.

**Source:** ADR 0071; the vocabulary is the Snapshot's.

## Comments

- `label_values.c` holds the vocabulary table and the formatting; `label_values_apply` was
  split out of `label_values_fill` so the rules are testable without a card or a Library. `fill`
  is the path lookup plus `apply`.
- The lookup is the four calls the ticket names, in RAM: `library_find` -> `library_kind` (a
  Track) -> `library_size` -> `snapshot_path_hash` -> `snapshot_find`. No filesystem call.
- **`t32_label` found a real bug:** `apply` did not clear first, so a second Track inherited the
  first's BPM, key and mood. It clears now, which is what makes "an unmeasured value prints
  nothing" true for every Track after the first.
- `t32_label` builds its own Snapshot image - header, one row, pool - so the formatting is tested
  against bytes the test owns: BPM rounds (1244 -> 124, 1245 -> 125), the key is Camelot
  (8 -> `8A`, 20 -> `8B`), 0xFF prints nothing, a measured zero prints `0`, the lowest mood bit
  wins, and every retired name returns `""`.
