#!/usr/bin/env python3
"""Pack the `label` section of every K7Art into one label sheet.

`docs/specs/k7art.md` is the container: §1 is the file this reads and §7 what it refuses. Its
label section, and the sheet it packs, are `docs/specs/label-templates.md`'s: §2 is the section,
§3 the sheet, §7 the refusals. It refuses rather than substitutes -- a design that does not fit
the panel is a build failure, not a design that quietly prints something else.

    python3 tools/label_pack.py assets/k7art build/      # write the sheet, label_fonts.h and the
                                                         # manifest (reads assets/k7art/*.json)
    python3 tools/label_pack.py assets/k7art build/ --check   # validate, write nothing
    python3 tools/label_pack.py --selftest
    python3 tools/label_pack.py --generate-vocabulary   # write label_vocab.h and the editor's block
    python3 tools/label_pack.py --check-vocabulary      # both current, and the faces the editor
                                                        # serves are the files the build compiles

The designs are the folder: every *.json in it is a K7Art and nothing is skipped silently. The
vocabulary is not a design and does not move with them: assets/labels/ keeps vocabulary.json and
the K7's pictures, and the font table is read from there.

Standard library only: the firmware build runs this, and the build has no network.
"""

import json
import re
import struct
import sys
import tempfile
import zlib
from pathlib import Path

MAGIC = b"K7LBL\0\0\0"
FORMAT = "k7art"
DESIGN_VERSION = 2
SHEET_VERSION = 5
HEADER_SIZE = 32
TEMPLATE_SIZE = 16
PRIM_SIZE = 24
SLOT_SIZE = 8

# Where the factory designs live. The folder is the set: every *.json in it is a K7Art - nothing
# is skipped silently. It is not the vocabulary's folder; assets/labels/ keeps vocabulary.json
# and the K7's pictures.
K7ART_DIR = "assets/k7art"

ROOT = Path(__file__).resolve().parent.parent
EDITOR = ROOT / "tools" / "LabelEditor" / "src" / "engine" / "vocab.generated.ts"
# The editor serves a face as a file, one per face at every size the sheet carries. Its
# name is the face's: "Archivo Narrow 600" is `archivo-narrow-600.ttf`.
EDITOR_FONTS = ROOT / "tools" / "LabelEditor" / "public" / "fonts"
VOCABULARY = "vocabulary.json"
VOCAB_HEADER = ROOT / "components" / "portable" / "label" / "include" / "label_vocab.h"
BLOCK_OPEN = "// <generated:label-vocabulary>"
BLOCK_CLOSE = "// </generated:label-vocabulary>"

# The table itself. The format's limits and the Slot vocabulary are the format's, not a
# property of the directory being packed, so they are read once here from the repository's
# copy; the faces are the directory's, and load_fonts reads them from the one being packed.
VOCAB = json.loads((ROOT / "assets" / "labels" / VOCABULARY).read_text(encoding="utf-8"))

SPACE_W, SPACE_H = VOCAB["space"]["w"], VOCAB["space"]["h"]
# The Card's window in the design's own space: (95,101) 556x166, radius 83.
APERTURE = tuple(VOCAB["aperture"][k] for k in ("x", "y", "w", "h", "r"))
# What a prim's record can hold, as the editor must refuse it too: x and y are int16, w and h
# uint16 (ADR 0072 - position beyond the print is not refused, only what the bytes cannot say).
COORD = VOCAB["coords"]

PRIM_MAX = VOCAB["limits"]["prims"]
SLOT_MAX = VOCAB["limits"]["slots"]
TEMPLATE_MAX = VOCAB["limits"]["templates"]
VALUE_MAX = VOCAB["value"]

CANVAS_MAX = VOCAB["canvas"]
FONT_ROLES = tuple(VOCAB["defaults"]["fonts"])
ALIGN_INDEX = {"left": 0, "center": 1, "right": 2}

# The device's vocabulary: one table, generated into the editor and the reader.
SLOT_NAMES = [slot["id"] for slot in VOCAB["slots"]]

# The J-card's sheet, folds and bounds (docs/specs/k7art.md 4 and 7). The packer never draws a
# J-card and never puts one in the sheet; it holds the pair's jcard to the format's refusals so a
# design the webapp cannot draw is a build failure naming itself.
JCARD_SPACE = (1300, 1280, 320)
JCARD_PARTS = {
    "flap": (0, 0, 320, 1280),
    "spine": (320, 0, 160, 1280),
    "cover": (480, 0, 820, 1280),
}
JCARD_FACES = tuple(VOCAB["defaults"]["fonts"][role]["ff"] for role in FONT_ROLES)
JCARD_SIZE_MIN, JCARD_SIZE_MAX = 4, 640
# A J-card's texts bind to the label's eleven Slots as well as the J-card's own three
# (docs/specs/k7art.md 4): the App fills the fields its surface carries and prints nothing
# for the rest. A field outside this list is one neither the App nor the print can draw.
JCARD_FIELDS = tuple(SLOT_NAMES) + ("title", "artist", "meta")
JCARD_PRIM_MAX = 64
JCARD_BYTES_MAX = 16 * 1024
JCARD_BOUND = 4096

ID_RE = re.compile(r"^k7\.[a-z0-9][a-z0-9-]*$")
HEX_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")
SLOT_ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")


class PackError(Exception):
    def __init__(self, errors):
        self.errors = list(errors)
        super().__init__("\n".join(self.errors))


def _hex(value, what, errors, allow_none=False):
    if value is None and allow_none:
        return None
    if not isinstance(value, str) or not HEX_RE.match(value):
        errors.append(f"{what}: {value!r} is not #RRGGBB")
        return 0
    return int(value[1:], 16)


def _int(value, what, errors, lo=None, hi=None, default=None):
    if value is None and default is not None:
        return default
    if isinstance(value, bool) or not isinstance(value, int):
        errors.append(f"{what}: {value!r} is not an integer")
        return 0
    if lo is not None and value < lo or hi is not None and value > hi:
        errors.append(f"{what}: {value} is outside {lo}..{hi}")
        return 0
    return value


class Pool:
    """The sheet's interned string pool: every distinct string once, NUL-terminated."""

    def __init__(self):
        self._bytes = bytearray()
        self._at = {}

    def add(self, text):
        if text not in self._at:
            self._at[text] = len(self._bytes)
            self._bytes += text.encode("utf-8") + b"\0"
        return self._at[text]

    def bytes(self):
        return bytes(self._bytes)


def _tag(prim, i):
    if prim.get("slot"):
        return f'slot "{prim["slot"]}"'
    if isinstance(prim.get("text"), str) and prim["text"]:
        return f'static text "{prim["text"][:24]}"'
    return f"prim {i}"


def jcard_errors(jcard, did):
    """The pair's J-card refusals (docs/specs/k7art.md 7). It never enters the sheet."""
    errors = []

    def add(line):
        errors.append(f"{did}: jcard {line}")

    space = jcard.get("space")
    got = (space.get("w"), space.get("h"), space.get("unit")) if isinstance(space, dict) else None
    if got != JCARD_SPACE:
        add(f"space is {got}, not {JCARD_SPACE[0]}x{JCARD_SPACE[1]} at "
            f"{JCARD_SPACE[2]} units per inch")

    parts = jcard.get("parts")
    if not isinstance(parts, dict):
        add("parts is missing -- the fold lines are the case's")
    else:
        for part, want in JCARD_PARTS.items():
            rect = parts.get(part)
            got = (tuple(rect.get(k) for k in ("x", "y", "w", "h"))
                   if isinstance(rect, dict) else None)
            if got != want:
                add(f'part "{part}" is {got}, not {want}')
        extra = sorted(set(parts) - set(JCARD_PARTS))
        if extra:
            add(f"parts names {', '.join(extra)}; the case is flap, spine and cover")

    _hex(jcard.get("background"), f"{did}: jcard background", errors)

    fonts = jcard.get("fonts")
    if not isinstance(fonts, dict):
        add("fonts is missing -- the three faces are the pair's")
        fonts = {}
    for role in FONT_ROLES:
        face = fonts.get(role) or {}
        ff, size = face.get("ff"), face.get("size")
        if ff not in JCARD_FACES:
            add(f"font role {role} is {ff!r}, not one of {', '.join(JCARD_FACES)}")
        if (isinstance(size, bool) or not isinstance(size, int)
                or not JCARD_SIZE_MIN <= size <= JCARD_SIZE_MAX):
            add(f"font role {role} size {size!r} is outside "
                f"{JCARD_SIZE_MIN}..{JCARD_SIZE_MAX} units")

    prims = jcard.get("prims")
    if not isinstance(prims, list):
        add("prims is not a list")
        prims = []
    if len(prims) > JCARD_PRIM_MAX:
        add(f"{len(prims)} prims, more than the {JCARD_PRIM_MAX} the App draws")

    for i, prim in enumerate(prims, 1):
        if not isinstance(prim, dict):
            add(f"prim {i}: not an object")
            continue
        kind = prim.get("t")
        if kind not in ("rect", "text"):
            add(f"prim {i}: t must be rect or text")
            continue
        x = _int(prim.get("x"), f"{did}: jcard prim {i}: x", errors,
                 -JCARD_BOUND, JCARD_BOUND)
        y = _int(prim.get("y"), f"{did}: jcard prim {i}: y", errors,
                 -JCARD_BOUND, JCARD_BOUND)
        w = _int(prim.get("w"), f"{did}: jcard prim {i}: w", errors, 1, JCARD_BOUND)
        _int(prim.get("rot", 0), f"{did}: jcard prim {i}: rot", errors, default=0)
        what = "fill" if kind == "rect" else "color"
        _hex(prim.get(what), f"{did}: jcard prim {i}: {what}", errors)
        if kind == "rect":
            h = _int(prim.get("h"), f"{did}: jcard prim {i}: h", errors, 1, JCARD_BOUND)
            _int(prim.get("r", 0), f"{did}: jcard prim {i}: r", errors, lo=0, default=0)
        else:
            h = 0
            if prim.get("align", "left") not in ALIGN_INDEX:
                add(f"prim {i}: align {prim.get('align')!r} is not left, center or right")
            if prim.get("font") not in FONT_ROLES:
                add(f"prim {i}: font {prim.get('font')!r} is not title, numeral or caption")
            if "slot" in prim:
                if prim["slot"] not in JCARD_FIELDS:
                    add(f'prim {i}: "{prim["slot"]}" is not a J-card field '
                        f'(a Slot, title, artist or meta)')
                if "preview" in prim and not isinstance(prim["preview"], str):
                    add(f"prim {i}: preview is not a string")
            elif "text" in prim:
                if not isinstance(prim["text"], str) or not prim["text"].strip():
                    add(f"prim {i}: a static text is empty")
            else:
                add(f"prim {i}: a text needs a slot or a text")
        if not (-JCARD_BOUND <= x and x + w <= JCARD_BOUND
                and -JCARD_BOUND <= y and y + h <= JCARD_BOUND):
            add(f"prim {i}: the box leaves the sheet's +/-{JCARD_BOUND} units")

    size = len(json.dumps(jcard, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))
    if size > JCARD_BYTES_MAX:
        add(f"the section is {size} B, over the {JCARD_BYTES_MAX} the App bundles")
    return errors


def pack_design(design, font_index, pool):
    """One K7Art v2 -> (template fields, prims, slots). Raises PackError(errors).

    The container names the design; the label section is the sheet's, and its `mask` is
    synthesised as the first rect (docs/specs/label-templates.md §2-§3). The J-card is checked
    against the pair's refusals and never enters the sheet.
    """
    errors = []
    did = design.get("id")
    before = len(errors)

    if design.get("format") != FORMAT:
        errors.append(f"{did}: format is {design.get('format')!r}, not {FORMAT!r}")
    if design.get("v") != DESIGN_VERSION:
        errors.append(f"{did}: v is not {DESIGN_VERSION} -- open a v1 design in the editor and export it")

    design_name = design.get("name")
    if not isinstance(design_name, str) or not design_name.strip():
        errors.append(f"{did}: no name -- the sheet's template is named for the log")
        design_name = did

    label = design.get("label")
    if not isinstance(label, dict):
        errors.append(f"{did}: no label -- the design is the pair")
        label = {}

    jcard = design.get("jcard")
    if not isinstance(jcard, dict):
        errors.append(f"{did}: no jcard -- the design is the pair")
    else:
        errors.extend(jcard_errors(jcard, did))

    space = label.get("space") or {}
    if (space.get("w"), space.get("h")) != (SPACE_W, SPACE_H):
        errors.append(f"{did}: space is {space.get('w')}x{space.get('h')}, not {SPACE_W}x{SPACE_H}")

    mask = label.get("mask")
    mask_prim = None
    if mask is not None:
        if not isinstance(mask, dict):
            errors.append(f"{did}: mask must be an object with color and r")
        else:
            colour = _hex(mask.get("color"), f"{did}: mask.color", errors)
            r = _int(mask.get("r", 0), f"{did}: mask.r", errors, lo=0, default=0)
            if colour is not None and r is not None:
                mask_prim = (colour, r)

    aperture = label.get("aperture")
    if aperture is not None:
        got = (aperture.get("x"), aperture.get("y"), aperture.get("w"),
               aperture.get("h"), aperture.get("r", 0))
        if got != APERTURE:
            errors.append(f"{did}: aperture {got} is not the Card's window {APERTURE}")
        if mask is None:
            errors.append(f"{did}: aperture without a mask -- the window is cut out of the paper")

    fonts = label.get("fonts")
    if not isinstance(fonts, dict):
        errors.append(f"{did}: no fonts -- a design without faces is type with no glyphs")
        fonts = {}
    font_ids = []
    for role in FONT_ROLES:
        entry = fonts.get(role) or {}
        key = (entry.get("ff"), entry.get("px"))
        if key not in font_index:
            errors.append(
                f"{did}: font role {role} is {key[0]!r} {key[1]!r}, "
                f"which assets/labels/vocabulary.json does not carry")
            font_ids.append(0)
        else:
            font_ids.append(font_index[key])

    canvas = label.get("canvas")
    canvas_colors = []
    if canvas is not None:
        if not isinstance(canvas, dict):
            errors.append(f"{did}: canvas must be an object with backgrounds and active")
        else:
            backgrounds = canvas.get("backgrounds")
            if not isinstance(backgrounds, list) or not 1 <= len(backgrounds) <= CANVAS_MAX:
                errors.append(
                    f"{did}: canvas.backgrounds must be one to {CANVAS_MAX} colours")
                backgrounds = []
            canvas_colors = [_hex(c, f"{did}: canvas.backgrounds[{i}]", errors)
                             for i, c in enumerate(backgrounds)]
            active = canvas.get("active")
            if (isinstance(active, bool) or not isinstance(active, int)
                    or not 0 <= active < len(canvas_colors)):
                errors.append(
                    f"{did}: canvas.active {active!r} is not an index into backgrounds")

    slots = label.get("slots") or {}
    if not isinstance(slots, dict):
        errors.append(f"{did}: slots must be an object")
        slots = {}
    if len(slots) > SLOT_MAX:
        errors.append(f"{did}: {len(slots)} slots, more than the {SLOT_MAX} allowed")
    slot_max = {}
    for name, spec in slots.items():
        if name not in SLOT_NAMES:
            errors.append(f'{did}: slot "{name}" is not in the vocabulary')
            continue
        slot_max[name] = _int((spec or {}).get("max"), f'{did}: slot "{name}".max',
                              errors, lo=1, hi=VALUE_MAX)

    prims = label.get("prims")
    if not isinstance(prims, list) or not prims:
        errors.append(f"{did}: no prims")
        prims = []
    # The sheet's cap counts the mask's rect, so the file may carry one fewer.
    if len(prims) + (1 if mask is not None else 0) > PRIM_MAX:
        errors.append(
            f"{did}: {len(prims) + (1 if mask is not None else 0)} prims, "
            f"more than the {PRIM_MAX} allowed")

    out_prims = []
    if mask_prim is not None:
        colour, r = mask_prim
        out_prims.append(struct.pack(
            "<BBBBhhHHHhII", 0, 0, 0, 0, 0, 0, SPACE_W, SPACE_H, r, 0,
            0xFFFFFFFF, colour))
    used_slots = set()
    for i, prim in enumerate(prims, 1):
        tag = f"{did}: {_tag(prim, i)}"
        kind = prim.get("t")
        x = _int(prim.get("x"), f"{tag}: x", errors, *COORD["x"])
        y = _int(prim.get("y"), f"{tag}: y", errors, *COORD["y"])
        w = _int(prim.get("w"), f"{tag}: w", errors, *COORD["w"])
        rot = _int(prim.get("rot", 0), f"{tag}: rot", errors, lo=-360, hi=360, default=0)
        if kind == "rect":
            h = _int(prim.get("h"), f"{tag}: h", errors, *COORD["h"])
            r = _int(prim.get("r", 0), f"{tag}: r", errors, lo=0, default=0)
            colour = _hex(prim.get("fill"), f"{tag}: fill", errors)
            out_prims.append(struct.pack(
                "<BBBBhhHHHhII", 0, 0, 0, 0, x, y, w, h, r,
                round(rot * 10) % 3600, 0xFFFFFFFF, colour or 0))
            continue

        if kind != "text":
            errors.append(f"{tag}: t must be rect or text")
            continue

        colour = _hex(prim.get("color"), f"{tag}: colour", errors)
        role = prim.get("font")
        if role not in FONT_ROLES:
            errors.append(f"{tag}: font {role!r} is not title, numeral or caption")
            font = 0
        else:
            font = FONT_ROLES.index(role)
        align = prim.get("align", "left")
        if align not in ALIGN_INDEX:
            errors.append(f"{tag}: align {align!r} is not left, center or right")
            align = 0
        else:
            align = ALIGN_INDEX[align]

        if "slot" in prim:
            name = prim["slot"]
            if name not in SLOT_NAMES:
                errors.append(f'{tag}: "{name}" is not a slot in the vocabulary')
                offset = pool.add(str(name))
            elif name not in slot_max:
                errors.append(f'{tag}: slot "{name}" is not declared')
                offset = pool.add(name)
            else:
                used_slots.add(name)
                offset = pool.add(name)
            kind_index = 2
        elif "text" in prim:
            text = prim.get("text")
            if not isinstance(text, str) or not text.strip():
                errors.append(f"{tag}: a static text is empty")
                text = ""
            offset = pool.add(text)
            kind_index = 1
        else:
            errors.append(f"{tag}: a text needs a slot or a text")
            continue

        out_prims.append(struct.pack(
            "<BBBBhhHHHhII", kind_index, 0, font, align, x, y, w, 0, 0,
            round(rot * 10) % 3600, offset, colour or 0))

    for name in slot_max:
        if name not in used_slots:
            errors.append(f'{did}: slot "{name}" is declared but no text uses it')

    if len(errors) > before:
        raise PackError(errors)

    slot_names = sorted(slot_max)
    slot_records = [struct.pack("<IB3x", pool.add(name), slot_max[name])
                    for name in slot_names]
    fields = {
        "name": pool.add(design_name),
        "prim_count": len(out_prims),
        "slot_count": len(slot_records),
        "font": font_ids,
        "canvas_count": len(canvas_colors),
        "canvases": canvas_colors,
    }
    return fields, out_prims, slot_records, slot_names


def manifest_text(rows):
    """The sheet as its inputs describe it, for a reader to be checked against.

    Written beside the sheet and read by host/tests/t33_label_sheet.c, which opens the bytes
    with the reader the firmware ships and compares the two. It is a second opinion derived
    from the designs, not a re-reading of the blob: a manifest read back out of the sheet
    would agree with it whatever the packer did.

    Tab-separated: the counts, the canvas list (comma-joined `#RRGGBB`, or `-` when the design
    has none), the Slots, and the design's name last - so a name containing a tab only makes the
    name longer. The slots are comma-joined: a Slot's name is an identifier.
    """
    lines = ["label_manifest\t3", f"count\t{len(rows)}"]
    for i, (name, prim_count, slot_names, canvas) in enumerate(rows):
        lines.append(
            f"{i}\t{prim_count}\t{len(slot_names)}\t{canvas}\t{','.join(slot_names)}\t{name}")
    return "\n".join(lines) + "\n"


def pack_sheet(designs, fonts):
    """Every design -> the sheet's bytes, the font header's text, and the manifest.

    Raises PackError."""
    errors = []
    if not designs:
        raise PackError([f"no designs: {K7ART_DIR}/ has no *.json to pack"])
    if len(designs) > TEMPLATE_MAX:
        raise PackError([f"{len(designs)} designs, more than the {TEMPLATE_MAX} a sheet holds"])

    font_index = {(f["name"], f["px"]): i for i, f in enumerate(fonts)}
    pool = Pool()
    ids = set()
    ordered = sorted(designs, key=lambda d: str(d.get("id")))
    for design in ordered:
        did = design.get("id")
        if not isinstance(did, str) or not ID_RE.match(did):
            errors.append(f'{did}: id must look like k7.my-name')
        if did in ids:
            errors.append(f"{did}: duplicate id")
        ids.add(did)

    templates, prims, slots, rows = [], [], [], []
    for design in ordered:
        try:
            fields, tp, ts, slot_names = pack_design(design, font_index, pool)
        except PackError as exc:
            errors.extend(exc.errors)
            continue
        fields["prim_start"] = len(prims)
        fields["slot_start"] = len(slots)
        prims.extend(tp)
        slots.extend(ts)
        templates.append(fields)
        rows.append((
            design.get("name") or str(did),
            len(tp),
            slot_names,
            ",".join(f"#{c:06X}" for c in fields["canvases"]) or "-",
        ))

    if errors:
        raise PackError(errors)

    pool_bytes = pool.bytes()
    total = (HEADER_SIZE + TEMPLATE_SIZE * len(templates)
             + CANVAS_MAX * 4 * len(templates)
             + PRIM_SIZE * len(prims) + SLOT_SIZE * len(slots) + len(pool_bytes))
    body = bytearray()
    for t in templates:
        # 16 B: name/counts/fonts/canvas_count. The literal colours are on the prims; the
        # canvas list follows the templates (docs/specs/label-templates.md §3.2).
        body += struct.pack(
            "<IHHHHBBBB",
            t["name"], t["prim_start"], t["prim_count"], t["slot_start"], t["slot_count"],
            t["font"][0], t["font"][1], t["font"][2], t["canvas_count"])
    for t in templates:
        for i in range(CANVAS_MAX):
            body += struct.pack("<I", t["canvases"][i] if i < t["canvas_count"] else 0)
    body += b"".join(prims)
    body += b"".join(slots)
    body += pool_bytes

    header = struct.pack(
        "<8sHHIIHHHHHH", MAGIC, SHEET_VERSION, HEADER_SIZE, total, zlib.crc32(bytes(body)),
        SPACE_W, SPACE_H, len(templates), len(prims), len(slots), len(pool_bytes))
    assert len(header) == HEADER_SIZE and len(body) == total - HEADER_SIZE

    font_header = (
        "/* Generated by tools/label_pack.py from assets/labels/vocabulary.json. Do not edit.\n"
        " * The sheet names a font by its index here; the app's k_label_font[] is this table. */\n"
        "#pragma once\n\n"
        + "".join(f"#define LABEL_FONT_{f['symbol'].upper()} {i}\n"
                  for i, f in enumerate(fonts))
        + f"\n#define LABEL_FONT_COUNT {len(fonts)}\n")
    return bytes(header) + bytes(body), font_header, manifest_text(rows)


def verify_sheet(blob):
    """Re-read a sheet the way the reader in components/portable/label/ will. Returns a
    one-line summary. Raises PackError if the bytes disagree with §3."""
    errors = []
    if len(blob) < HEADER_SIZE:
        raise PackError(["the sheet is shorter than its header"])
    (magic, version, header_size, total, crc, sw, sh,
     templates, prims, slots, pool_size) = struct.unpack_from("<8sHHIIHHHHHH", blob, 0)
    if magic != MAGIC:
        errors.append("magic is not K7LBL")
    if version != SHEET_VERSION:
        errors.append(f"version is {version}, not {SHEET_VERSION}")
    if header_size != HEADER_SIZE:
        errors.append(f"header_size is {header_size}, not {HEADER_SIZE}")
    want = (HEADER_SIZE + TEMPLATE_SIZE * templates + CANVAS_MAX * 4 * templates
            + PRIM_SIZE * prims + SLOT_SIZE * slots + pool_size)
    if total != want or total != len(blob):
        errors.append(f"total_size {total} disagrees with the parts ({want}) and the file ({len(blob)})")
    if (sw, sh) != (SPACE_W, SPACE_H):
        errors.append(f"space is {sw}x{sh}, not {SPACE_W}x{SPACE_H}")
    if not 1 <= templates <= TEMPLATE_MAX:
        errors.append(f"{templates} templates")
    if crc != zlib.crc32(blob[HEADER_SIZE:]):
        errors.append("crc32 disagrees with the body")
    pool_start = total - pool_size
    canvas_start = HEADER_SIZE + TEMPLATE_SIZE * templates
    for i in range(templates):
        (_, p_start, p_count, s_start, s_count, f0, f1, f2, canvas_count,
         ) = struct.unpack_from("<IHHHHBBBB", blob, HEADER_SIZE + i * TEMPLATE_SIZE)
        if p_start + p_count > prims or s_start + s_count > slots:
            errors.append(f"template {i} runs past its arrays")
        if canvas_count > CANVAS_MAX:
            errors.append(f"template {i}: {canvas_count} canvases, more than {CANVAS_MAX}")
            continue
        for j in range(CANVAS_MAX):
            colour = struct.unpack_from("<I", blob, canvas_start + (i * CANVAS_MAX + j) * 4)[0]
            if j < canvas_count:
                if colour > 0xFFFFFF:
                    errors.append(f"template {i}: canvas {j} is above RGB888")
            elif colour != 0:
                errors.append(f"template {i}: canvas {j} is past the design's {canvas_count}")
    for i in range(prims):
        off = canvas_start + CANVAS_MAX * 4 * templates + i * PRIM_SIZE
        kind, reserved, font, align, x, y, w, h, r, rot, text_off, colour = struct.unpack_from(
            "<BBBBhhHHHhII", blob, off)
        if kind == 0 and text_off != 0xFFFFFFFF:
            errors.append(f"prim {i}: a rect carries a text offset")
        if kind != 0:
            if not 0 <= text_off < pool_size:
                errors.append(f"prim {i}: text offset {text_off} is outside the pool")
            elif blob.find(b"\0", pool_start + text_off) < 0:
                errors.append(f"prim {i}: text is not NUL-terminated")
        if reserved != 0:
            errors.append(f"prim {i}: the reserved byte is not zero")
        if colour > 0xFFFFFF:
            errors.append(f"prim {i}: colour {colour:#08x} is above RGB888")
        if font > 2 or align > 2:
            errors.append(f"prim {i}: kind/font/align out of range")
    if errors:
        raise PackError(errors)
    return (f"{templates} designs, {prims} prims, {slots} slots, {pool_size} B pool, "
            f"{len(blob)} B, crc 0x{crc:08X}")


def load_fonts():
    """The sheet's font table: the faces the build compiles, in index order.

    The table lives in assets/labels/vocabulary.json, beside the K7's pictures and not with the
    designs, because it is the format's, not a design's: a face the editor offers and the build
    cannot compile is exactly the drift it exists to stop. `editor: true` marks the faces the
    editor offers and can draw.
    """
    path = ROOT / "assets" / "labels" / VOCABULARY
    data = json.loads(path.read_text(encoding="utf-8"))
    fonts = data.get("fonts")
    if not isinstance(fonts, list) or not fonts:
        raise PackError([f"{path}: no fonts"])
    for i, f in enumerate(fonts):
        if not isinstance(f, dict) or not f.get("name") or not isinstance(f.get("px"), int) \
                or not f.get("symbol"):
            raise PackError([f"{path}: font {i} needs name, px and symbol"])
    return fonts


def load_designs(src):
    """Every *.json in <src> is a K7Art; a file that is not one is an error.

    <src> is the K7Art folder itself (`assets/k7art` in the build). Nothing is skipped silently:
    a design left in the folder and not packed would be a Track that prints the wrong label and
    no line saying why. A name starting with a dot is not a design (an Export's zip, a
    .DS_Store or an AppleDouble sidecar), which is the one thing ignored, and it is ignored by
    name rather than by guessing. The retired `label-template v1` file and the old Export's
    labels.json are refused by name, with where to take them.
    """
    root = Path(src)
    paths = sorted(p for p in root.glob("*.json") if not p.name.startswith("."))
    if not paths:
        raise PackError([f"no designs: {root} has no *.json to pack"])

    errors = []
    designs = []
    for path in paths:
        try:
            design = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            errors.append(f"{path}: {exc}")
            continue
        if not isinstance(design, dict):
            errors.append(f"{path}: not a JSON object, so not a K7Art")
            continue
        if "format" not in design:
            if "templates" in design:
                errors.append(f"{path}: that is the old Export's labels.json manifest, not a "
                              f"design; import it in the editor and export a K7Art into {root}")
            elif all(k in design for k in ("v", "space", "prims")):
                errors.append(f"{path}: that is a label-template v1 file, the retired format; "
                              f"import it in the editor and export a K7Art into {root}")
            else:
                errors.append(f"{path}: not a K7Art (it needs format, v, id, name, inks, label "
                              f"and jcard)")
            continue
        designs.append(design)
    if errors:
        raise PackError(errors)
    return designs


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = {a for a in argv[1:] if a.startswith("--")}

    if "--selftest" in flags:
        return selftest()

    if "--generate-vocabulary" in flags:
        try:
            written = generate_vocabulary()
        except PackError as exc:
            for line in exc.errors:
                print(f"label_pack: {line}", file=sys.stderr)
            return 1
        for path in written:
            print(f"label_pack: wrote {path.relative_to(ROOT)}")
        return 0

    if "--check-vocabulary" in flags:
        try:
            errors = check_vocabulary()
        except PackError as exc:
            errors = exc.errors
        for line in errors:
            print(f"label_pack: {line}", file=sys.stderr)
        if errors:
            return 1
        print("label_pack: the vocabulary, its generated copies and the editor's faces agree")
        return 0

    if len(args) != 2:
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        print("usage: label_pack.py <assets/k7art> <out-dir> [--check]", file=sys.stderr)
        return 2
    src, out = Path(args[0]), Path(args[1])
    try:
        fonts = load_fonts()
        designs = load_designs(src)
        blob, font_header, manifest = pack_sheet(designs, fonts)
        summary = verify_sheet(blob)
    except PackError as exc:
        for line in exc.errors:
            print(f"label_pack: {line}", file=sys.stderr)
        return 1

    print(f"label_pack: {summary}")
    if "--check" in flags:
        print("label_pack: --check, nothing written")
        return 0
    out.mkdir(parents=True, exist_ok=True)
    (out / "label_sheet.bin").write_bytes(blob)
    (out / "label_fonts.h").write_text(font_header, encoding="utf-8")
    (out / "label_manifest.txt").write_text(manifest, encoding="utf-8")
    return 0


def vocabulary_errors(v):
    """The table is hand-edited, so it is checked before anything is generated from it.

    A duplicate Slot or a default font role naming a pair the sheet does not carry would
    otherwise reach the header and the page as a design nobody can pack.
    """
    errors = []
    if v.get("v") != 2:
        errors.append(f"{VOCABULARY}: v is {v.get('v')}, not 2")

    space = v.get("space") or {}
    if not isinstance(space.get("w"), int) or not isinstance(space.get("h"), int):
        errors.append(f"{VOCABULARY}: space needs integer w and h")

    if not isinstance(v.get("value"), int) or not 1 <= v["value"] <= 255:
        errors.append(f"{VOCABULARY}: value is {v.get('value')!r}, not 1..255")

    canvas = v.get("canvas")
    if not isinstance(canvas, int) or not 1 <= canvas <= 8:
        errors.append(f"{VOCABULARY}: canvas is {canvas!r}, not 1..8 presets")

    limits = v.get("limits") or {}
    for key in ("prims", "slots", "templates"):
        if not isinstance(limits.get(key), int) or limits[key] < 1:
            errors.append(f"{VOCABULARY}: limits.{key} is {limits.get(key)!r}, not a positive int")

    coords = v.get("coords") or {}
    for axis in ("x", "y", "w", "h"):
        pair = coords.get(axis)
        if (not isinstance(pair, list) or len(pair) != 2
                or not all(isinstance(n, int) for n in pair) or pair[1] < pair[0]):
            errors.append(f"{VOCABULARY}: coords.{axis} is {pair!r}, not [lo, hi]")

    seen = set()
    for i, slot in enumerate(v.get("slots") or []):
        sid = slot.get("id")
        if not isinstance(sid, str) or not SLOT_ID_RE.match(sid):
            errors.append(f'{VOCABULARY}: slot {i} id {sid!r} is not a lowercase identifier')
            continue
        if sid in seen:
            errors.append(f'{VOCABULARY}: slot "{sid}" is listed twice')
        seen.add(sid)
        for key in ("label", "sim"):
            if not isinstance(slot.get(key), str):
                errors.append(f'{VOCABULARY}: slot "{sid}" needs a string {key}')
    if not v.get("slots"):
        errors.append(f"{VOCABULARY}: no slots")
    if len(v.get("slots") or []) > SLOT_MAX:
        errors.append(f"{VOCABULARY}: {len(v['slots'])} slots, more than the {SLOT_MAX} a sheet holds")

    symbols, pairs = set(), set()
    for i, font in enumerate(v.get("fonts") or []):
        name, px, symbol = font.get("name"), font.get("px"), font.get("symbol")
        if not isinstance(name, str) or not isinstance(px, int) or not isinstance(symbol, str):
            errors.append(f"{VOCABULARY}: font {i} needs name, px and symbol")
            continue
        if symbol in symbols:
            errors.append(f'{VOCABULARY}: font symbol "{symbol}" is used twice')
        if (name, px) in pairs:
            errors.append(f'{VOCABULARY}: {name} at {px} px is listed twice')
        symbols.add(symbol)
        pairs.add((name, px))
    if not v.get("fonts"):
        errors.append(f"{VOCABULARY}: no fonts")

    offered = [f"{name} {px}" for name, px in pairs if _is_editor_face(v, name, px)]
    if not offered:
        errors.append(f'{VOCABULARY}: no font carries "editor": true, so the page offers none')

    defaults = (v.get("defaults") or {}).get("fonts") or {}
    if set(defaults) != set(FONT_ROLES):
        errors.append(f"{VOCABULARY}: defaults.fonts names {sorted(defaults)}, not {list(FONT_ROLES)}")
    for role, face in defaults.items():
        if (face.get("ff"), face.get("px")) not in pairs:
            errors.append(f'{VOCABULARY}: defaults.fonts.{role} is {face.get("ff")!r} at '
                          f'{face.get("px")!r}, which the sheet does not carry')
        elif not _is_editor_face(v, face.get("ff"), face.get("px")):
            errors.append(f'{VOCABULARY}: defaults.fonts.{role} is not an editor face, so the '
                          f'page cannot draw the design it starts')
    return errors


def _is_editor_face(v, name, px):
    return any(f.get("name") == name and f.get("px") == px and f.get("editor")
               for f in v.get("fonts") or [])


def vocab_header(v):
    """The vocabulary as the firmware sees it: the limits, and the one Slot list.

    Committed rather than generated at build time, like the font_*.c files: the portable
    component compiles on the host too, and `make -C host` has no Python in it.
    """
    rows = [
        "/* Generated by tools/label_pack.py from assets/labels/vocabulary.json. Do not edit.",
        " *",
        " * The label vocabulary: what a design may say, and the bounds the sheet and the panel",
        " * hold it to. `make -C host label` refuses this file when it and the vocabulary",
        " * disagree, so the editor, the packer and the firmware read one table.",
        " *",
        " * The Slot list is one list: the enum the resolver indexes and the names the sheet",
        " * carries are both expansions of it, so a Slot cannot exist in one and not the other.",
        " */",
        "#pragma once",
        "",
        f"#define LABEL_SPACE_W       {v['space']['w']}u",
        f"#define LABEL_SPACE_H       {v['space']['h']}u",
        f"#define LABEL_VALUE_MAX     {v['value']}u",
        f"#define LABEL_CANVAS_MAX    {v['canvas']}u",
        f"#define LABEL_PRIM_MAX      {v['limits']['prims']}u",
        f"#define LABEL_SLOT_MAX      {v['limits']['slots']}u",
        f"#define LABEL_TEMPLATE_MAX  {v['limits']['templates']}u",
        "",
        "#define LABEL_SLOT_LIST(X) \\",
    ]
    slots = v["slots"]
    for i, s in enumerate(slots):
        tail = "" if i == len(slots) - 1 else " \\"
        rows.append(f'    X(LABEL_SLOT_{s["id"].upper()}, "{s["id"]}"){tail}')
    rows += [
        "",
        "typedef enum {",
        "#define LABEL_SLOT_ENUM(id, name) id,",
        "    LABEL_SLOT_LIST(LABEL_SLOT_ENUM)",
        "#undef LABEL_SLOT_ENUM",
        "    LABEL_SLOT_COUNT",
        "} label_slot_id_t;",
    ]
    return "\n".join(rows) + "\n"


def vocab_block(v):
    """The vocabulary as the editor imports it, markers included.

    The editor is a module graph, not a page opened from disk, so the table is a module of
    its own that `engine/types.ts` and `engine/vocab.ts` import. Written as a hand-shaped
    literal rather than dumped JSON, because this block is committed and read in review.
    """
    def js(value):
        return json.dumps(value, ensure_ascii=False)

    sp, ap, lm = v["space"], v["aperture"], v["limits"]
    cd, df = v["coords"], v["defaults"]["fonts"]
    rows = [
        BLOCK_OPEN,
        "// assets/labels/vocabulary.json is the source. tools/label_pack.py writes this",
        "// block, and `make -C host label` refuses it when it is stale. Do not edit: the",
        "// vocabulary is the source, and this is the editor's copy of it.",
        "export const VOCAB = {",
        f"  space: {{ w: {sp['w']}, h: {sp['h']} }},",
        f"  aperture: {{ x: {ap['x']}, y: {ap['y']}, w: {ap['w']}, h: {ap['h']}, "
        f"r: {ap['r']} }},",
        f"  value: {v['value']},",
        f"  canvas: {v['canvas']},",
        f"  limits: {{ prims: {lm['prims']}, slots: {lm['slots']}, "
        f"templates: {lm['templates']} }},",
        "  coords: {",
    ]
    for axis in ("x", "y", "w", "h"):
        lo, hi = cd[axis]
        rows.append(f"    {axis}: [{lo}, {hi}],")
    rows += ["  },", "  slots: ["]
    for slot in v["slots"]:
        rows.append(f"    {{ id: {js(slot['id'])}, label: {js(slot['label'])}, "
                    f"sim: {js(slot['sim'])} }},")
    rows += ["  ],", "  fonts: ["]
    for font in v["fonts"]:
        extra = ', editor: true' if font.get("editor") else ""
        rows.append(f"    {{ name: {js(font['name'])}, px: {font['px']}, "
                    f"symbol: {js(font['symbol'])}{extra} }},")
    rows += ["  ],", "  defaults: { fonts: {"]
    for role, face in df.items():
        rows.append(f"    {role}: {{ ff: {js(face['ff'])}, px: {face['px']} }},")
    rows += ["  } },", "} as const;", BLOCK_CLOSE]
    return "\n".join(rows) + "\n"


def block_span(text):
    """The character span of the generated block, markers included."""
    i, j = text.find(BLOCK_OPEN), text.find(BLOCK_CLOSE)
    if i < 0 or j < 0 or j < i:
        raise PackError([f"{EDITOR}: no {BLOCK_OPEN} ... {BLOCK_CLOSE} block to write"])
    start = text.rfind("\n", 0, i) + 1
    end = text.find("\n", j)
    return start, (len(text) if end < 0 else end + 1)


def _face_file(name):
    """The file the editor serves a face from: its name, lowercased and hyphenated."""
    return EDITOR_FONTS / (re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") + ".ttf")


def _editor_faces(v):
    """The faces the editor offers, as files the build compiles.

    The editor draws a face from `public/fonts/`, so a face it offers that is missing there
    cannot be drawn, and one whose bytes differ from `assets/fonts/` is a face the panel
    will not print - the designer would see glyphs the sheet does not carry. The bytes are
    compared, not just the name.
    """
    errors = []
    names = sorted({f["name"] for f in v["fonts"] if f.get("editor")})
    for name in names:
        served = _face_file(name)
        compiled = ROOT / "assets" / "fonts" / served.name
        shown = served.relative_to(ROOT)
        if not compiled.exists():
            errors.append(f"{VOCABULARY}: {name!r} is offered to the editor, and "
                          f"assets/fonts/{served.name} is not a file the build compiles")
        elif not served.exists():
            errors.append(f"{shown}: missing; the editor offers {name!r} and cannot draw it")
        elif served.read_bytes() != compiled.read_bytes():
            errors.append(f"{shown}: not the bytes of assets/fonts/{served.name}, so the "
                          f"editor draws a face the build does not compile")
    return errors


def generate_vocabulary():
    """Write label_vocab.h and the editor's block. Returns the two paths written."""
    v = VOCAB
    errors = vocabulary_errors(v)
    if errors:
        raise PackError(errors)
    header = vocab_header(v)
    VOCAB_HEADER.parent.mkdir(parents=True, exist_ok=True)
    VOCAB_HEADER.write_text(header, encoding="utf-8")
    if EDITOR.exists():
        text = EDITOR.read_text(encoding="utf-8")
        start, end = block_span(text)
        EDITOR.write_text(text[:start] + vocab_block(v) + text[end:], encoding="utf-8")
    else:
        # The block is the whole file: it is generated, and nothing outside the markers
        # belongs in it.
        EDITOR.parent.mkdir(parents=True, exist_ok=True)
        EDITOR.write_text(vocab_block(v), encoding="utf-8")
    return [VOCAB_HEADER, EDITOR]


def check_vocabulary():
    """Everything the vocabulary generates is current, and the editor draws compiled faces."""
    v = VOCAB
    errors = vocabulary_errors(v)
    header = vocab_header(v)
    if not VOCAB_HEADER.exists():
        errors.append(f"{VOCAB_HEADER}: missing; run --generate-vocabulary")
    elif VOCAB_HEADER.read_text(encoding="utf-8") != header:
        errors.append(f"{VOCAB_HEADER}: stale; run --generate-vocabulary")

    if not EDITOR.exists():
        errors.append(f"{EDITOR}: missing; run --generate-vocabulary")
    else:
        text = EDITOR.read_text(encoding="utf-8")
        try:
            start, end = block_span(text)
        except PackError as exc:
            return errors + exc.errors
        if text[start:end] != vocab_block(v):
            errors.append(f"{EDITOR}: the generated block is stale; run --generate-vocabulary")

    return errors + _editor_faces(v)


def selftest():
    fonts = [{"name": "Jura", "px": 20, "symbol": "jura_20"},
             {"name": "Archivo Narrow 600", "px": 32, "symbol": "archivo_narrow_600_32"}]
    good = {
        "format": "k7art", "v": 2, "id": "k7.self", "name": "Self",
        "label": {
            "space": {"w": 748, "h": 359},
            "mask": {"color": "#F2EDE3", "r": 19},
            "aperture": {"x": 95, "y": 101, "w": 556, "h": 166, "r": 83},
            "canvas": {"backgrounds": ["#101010", "#F5F1E6"], "active": 1},
            "fonts": {"title": {"ff": "Archivo Narrow 600", "px": 32},
                      "numeral": {"ff": "Jura", "px": 20},
                      "caption": {"ff": "Jura", "px": 20}},
            "slots": {"song_title": {"max": 24}},
            "prims": [
                {"t": "rect", "x": 0, "y": 300, "w": 40, "h": 20, "r": 0, "fill": "#C63A2E"},
                {"t": "text", "slot": "song_title", "x": 32, "y": 280, "w": 400,
                 "font": "title", "color": "#24211D", "align": "left", "preview": "Neon Skyline"},
                {"t": "text", "text": "SIDE A", "x": 32, "y": 20, "w": 200,
                 "font": "caption", "color": "#B9AC8E", "align": "right"},
            ],
        },
        "jcard": {
            "space": {"w": 1300, "h": 1280, "unit": 320},
            "parts": {"flap": {"x": 0, "y": 0, "w": 320, "h": 1280},
                      "spine": {"x": 320, "y": 0, "w": 160, "h": 1280},
                      "cover": {"x": 480, "y": 0, "w": 820, "h": 1280}},
            "background": "#F2EDE3",
            "fonts": {"title": {"ff": "Archivo Narrow 600", "size": 56},
                      "numeral": {"ff": "Barlow Condensed 700", "size": 136},
                      "caption": {"ff": "IBM Plex Mono 500", "size": 31}},
            "prims": [
                {"t": "text", "slot": "title", "x": 540, "y": 900, "w": 700,
                 "font": "numeral", "color": "#24211D", "align": "left", "preview": "Neon Skyline"},
                # A J-card text binds to the label's Slots as well as its own three (k7art.md 4).
                {"t": "text", "slot": "song_artist", "x": 540, "y": 1060, "w": 700,
                 "font": "title", "color": "#24211D", "align": "left", "preview": "The Analog Sons"},
                {"t": "text", "slot": "meta", "x": 540, "y": 1120, "w": 700,
                 "font": "caption", "color": "#24211D", "align": "left",
                 "preview": "Side A · Side B"},
            ],
        },
    }

    def expect_refusal(mutate, needle):
        bad = json.loads(json.dumps(good))
        mutate(bad)
        try:
            pack_sheet([bad], fonts)
        except PackError as exc:
            if any(needle in e for e in exc.errors):
                return
            raise SystemExit(f"selftest: expected {needle!r}, got {exc.errors}")
        raise SystemExit(f"selftest: expected a refusal containing {needle!r}")

    blob, header, manifest = pack_sheet([good], fonts)
    summary = verify_sheet(blob)
    # The mask's rect is synthesised in front of the design's prims.
    assert "1 designs, 4 prims, 1 slots" in summary, summary
    assert "LABEL_FONT_JURA_20 0" in header and "LABEL_FONT_COUNT 2" in header
    # The manifest is what t33 compares the opened sheet against, so its shape is part of
    # the packer's contract: index, prim count, slot count, the canvas list, the packed slot
    # names, the name.
    assert manifest == ("label_manifest\t3\ncount\t1\n"
                        "0\t4\t1\t#101010,#F5F1E6\tsong_title\tSelf\n"), repr(manifest)
    # Version 5: a 16-byte template with no inks; the canvas list follows the templates.
    fields = struct.unpack_from("<IHHHHBBBB", blob, HEADER_SIZE)
    assert fields[8] == 2, fields
    canvas_at = HEADER_SIZE + TEMPLATE_SIZE
    assert struct.unpack_from("<II", blob, canvas_at) == (0x101010, 0xF5F1E6)
    assert struct.unpack_from("<II", blob, canvas_at + 8) == (0, 0)
    # The mask's prim is the first; it paints the mask's colour and is a rect.
    prim_at = canvas_at + CANVAS_MAX * 4
    prim = struct.unpack_from("<BBBBhhHHHhII", blob, prim_at)
    assert prim[0] == 0 and prim[1] == 0 and prim[11] == 0xF2EDE3, prim
    no_canvas = json.loads(json.dumps(good))
    del no_canvas["label"]["canvas"]
    bare, _, bare_manifest = pack_sheet([no_canvas], fonts)
    assert verify_sheet(bare)
    fields = struct.unpack_from("<IHHHHBBBB", bare, HEADER_SIZE)
    assert fields[8] == 0, fields
    assert struct.unpack_from("<II", bare, HEADER_SIZE + TEMPLATE_SIZE) == (0, 0)
    assert bare_manifest.endswith("0\t4\t1\t-\tsong_title\tSelf\n"), bare_manifest

    # the container is the identity: format, version and the k7.<slug> id are the file's
    expect_refusal(lambda d: d.update(format="label-template"), "format is 'label-template'")
    expect_refusal(lambda d: d.update(v=1), "v is not 2")
    expect_refusal(lambda d: d.update(id="label.self"), "id must look like k7.my-name")
    expect_refusal(lambda d: d.update(name="  "), "no name")
    expect_refusal(lambda d: d.pop("label"), "no label")
    expect_refusal(lambda d: d.pop("jcard"), "no jcard")
    try:
        pack_sheet([good, good], fonts)
        raise SystemExit("selftest: the same id twice was accepted")
    except PackError as exc:
        assert any("duplicate id" in e for e in exc.errors), exc.errors
    try:
        pack_sheet([], fonts)
        raise SystemExit("selftest: an empty folder was accepted")
    except PackError as exc:
        assert "no designs" in exc.errors[0], exc.errors

    # the label section is the sheet's, and every rule the sheet had still refuses by name
    expect_refusal(lambda d: d["label"].update(space={"w": 800, "h": 359}), "space is 800x359")
    expect_refusal(lambda d: d["label"].update(
        aperture={"x": 0, "y": 0, "w": 556, "h": 166, "r": 83}), "is not the Card's window")
    # a design may bleed past the print; the print crops it (ADR 0072)
    bleed = json.loads(json.dumps(good))
    bleed["label"]["prims"][0].update(x=400)
    pack_sheet([bleed], fonts)
    expect_refusal(lambda d: d["label"]["prims"][1].update(font="numeral2"), "font 'numeral2'")
    expect_refusal(lambda d: d["label"]["prims"][1].update(slot="year"),
                   "not a slot in the vocabulary")
    expect_refusal(lambda d: d["label"]["fonts"].update(title={"ff": "Nope", "px": 12}),
                   "does not carry")
    # every element carries its colour: a role is not one, and neither is a broken hex
    expect_refusal(lambda d: d["label"]["prims"][0].update(fill="band.0"), "fill")
    expect_refusal(lambda d: d["label"]["prims"][1].update(color="ink"), "colour")
    expect_refusal(lambda d: d["label"].update(mask={"color": "paper", "r": 10}), "mask.color")
    expect_refusal(lambda d: d["label"].update(mask={"color": "#123456", "r": -1}), "mask.r")
    expect_refusal(lambda d: d["label"].pop("mask"), "aperture without a mask")
    expect_refusal(lambda d: d["label"].update(canvas={"backgrounds": [], "active": 0}),
                   "canvas.backgrounds must be one to 8")
    expect_refusal(lambda d: d["label"].update(
        canvas={"backgrounds": ["#101010"] * 9, "active": 0}),
        "canvas.backgrounds must be one to 8")
    expect_refusal(lambda d: d["label"].update(canvas={"backgrounds": ["#nope"], "active": 0}),
                   "canvas.backgrounds[0]")
    expect_refusal(lambda d: d["label"].update(canvas={"backgrounds": ["#101010"], "active": 2}),
                   "canvas.active 2")

    # The J-card is checked against the pair's refusals and is never packed.
    expect_refusal(lambda d: d["jcard"].update(space={"w": 1200, "h": 1280, "unit": 320}),
                   "jcard space is (1200, 1280, 320), not 1300x1280 at 320 units per inch")
    expect_refusal(lambda d: d["jcard"]["parts"].update(
        spine={"x": 320, "y": 0, "w": 200, "h": 1280}), 'jcard part "spine"')
    expect_refusal(lambda d: d["jcard"]["parts"].update(
        back={"x": 0, "y": 0, "w": 10, "h": 10}), "jcard parts names back")
    expect_refusal(lambda d: d["jcard"]["fonts"]["title"].update(ff="Jura"), "not one of")
    expect_refusal(lambda d: d["jcard"]["fonts"]["title"].update(size=3), "outside 4..640")
    expect_refusal(lambda d: d["jcard"].pop("background"), "jcard background")
    expect_refusal(lambda d: d["jcard"]["prims"][1].update(slot="year"),
                   '"year" is not a J-card field')
    expect_refusal(lambda d: d["jcard"]["prims"][1].update(color="ink"),
                   "jcard prim 2: color")
    expect_refusal(lambda d: d["jcard"]["prims"][0].update(t="circle"), "t must be rect or text")
    expect_refusal(lambda d: d["jcard"]["prims"][1].update(x=JCARD_BOUND + 1),
                   "outside -4096..4096")
    expect_refusal(lambda d: d["jcard"]["prims"][0].update(x=1, w=JCARD_BOUND),
                   "the box leaves the sheet's +/-4096 units")
    expect_refusal(lambda d: d["jcard"]["prims"].extend(
        [{"t": "rect", "x": 0, "y": 0, "w": 4, "h": 4, "fill": "#111111"}] * 62),
        "more than the 64 the App draws")
    expect_refusal(lambda d: d["jcard"]["prims"].append(
        {"t": "text", "text": "x" * JCARD_BYTES_MAX, "x": 0, "y": 0, "w": 100,
         "font": "title", "color": "#111111", "align": "left"}),
        "over the 16384 the App bundles")

    # a folder is the set: every *.json in it is a K7Art, a stray file fails naming itself, the
    # retired format and the old Export's manifest say what they are, and a dot-name is not one
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        try:
            load_designs(d)
            raise SystemExit("selftest: an empty K7Art folder was accepted")
        except PackError as exc:
            assert "no *.json" in exc.errors[0], exc.errors

        (root / "k7.self.json").write_text(json.dumps(good))
        (root / ".DS_Store").write_text("junk")
        (root / "._k7.self.json").write_text("junk")
        assert [x["id"] for x in load_designs(d)] == ["k7.self"], "the dot-names were read"

        (root / "labels.json").write_text(json.dumps({"v": 1, "templates": []}))
        try:
            load_designs(d)
            raise SystemExit("selftest: the old Export's manifest was accepted as a design")
        except PackError as exc:
            assert "labels.json manifest" in exc.errors[0], exc.errors

        (root / "labels.json").unlink()
        retired = {"v": 1, "id": "label.self", "name": "Self", "space": {"w": 748, "h": 359},
                   "prims": good["label"]["prims"]}
        (root / "label.self.json").write_text(json.dumps(retired))
        try:
            load_designs(d)
            raise SystemExit("selftest: the retired label-template v1 was accepted")
        except PackError as exc:
            assert "retired format" in exc.errors[0] and "label.self.json" in exc.errors[0], \
                exc.errors

        (root / "label.self.json").unlink()
        (root / "stray.json").write_text(json.dumps({"hello": 1}))
        try:
            load_designs(d)
            raise SystemExit("selftest: a file that is not a K7Art was accepted")
        except PackError as exc:
            assert "not a K7Art" in exc.errors[0], exc.errors

    print("label_pack: selftest ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
