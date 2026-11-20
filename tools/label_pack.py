#!/usr/bin/env python3
"""Pack `label-template v1` designs into one label sheet.

`docs/specs/label-templates.md` is the contract: §2 is the design file this reads, §3 is the
sheet it writes, §7 is the list of things it refuses. It refuses rather than substitutes -- a
design that does not fit the panel is a build failure, not a design that quietly prints
something else.

    python3 tools/label_pack.py assets/labels build/      # write the sheet, label_fonts.h and the
                                                          # manifest (reads templates/*.json)
    python3 tools/label_pack.py assets/labels build/ --check   # validate, write, then re-read
    python3 tools/label_pack.py --selftest
    python3 tools/label_pack.py --generate-vocabulary   # write label_vocab.h and the editor's block
    python3 tools/label_pack.py --check-vocabulary      # both current, and the faces the editor
                                                        # serves are the files the build compiles

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
DESIGN_VERSION = 1
SHEET_VERSION = 3
HEADER_SIZE = 32
TEMPLATE_SIZE = 64
PRIM_SIZE = 24
SLOT_SIZE = 8

# The designs sit in their own folder: assets/labels/ also holds the K7 art (k7logo.png,
# cover.jpg), and a sheet is packed from templates/ alone. Every *.json in it is a design -
# nothing is skipped silently - and vocabulary.json stays at the root because it is the
# vocabulary and the font table, not a design.
TEMPLATES = "templates"

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

BAND_MAX = VOCAB["bands"]
CANVAS_MAX = VOCAB["canvas"]
ROLE_INDEX = {"paper": 0, "ink": 1, "rule": 2}
ROLE_INDEX.update({f"band.{i}": 3 + i for i in range(BAND_MAX)})
FILL_LITERAL = 3 + BAND_MAX
FONT_ROLES = tuple(VOCAB["defaults"]["fonts"])
TEXT_ROLES = ("ink",) + tuple(f"band.{i}" for i in range(BAND_MAX))
ALIGN_INDEX = {"left": 0, "center": 1, "right": 2}

# The device's vocabulary: one table, generated into the editor and the reader.
SLOT_NAMES = [slot["id"] for slot in VOCAB["slots"]]

ID_RE = re.compile(r"^label\.[a-z0-9][a-z0-9-]*$")
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


def pack_design(design, font_index, pool):
    """One design -> (template fields, prims, slots). Raises PackError(errors)."""
    errors = []
    did = design.get("id")
    before = len(errors)

    if design.get("v") != DESIGN_VERSION:
        errors.append(f"{did}: v is not {DESIGN_VERSION}")

    space = design.get("space") or {}
    if (space.get("w"), space.get("h")) != (SPACE_W, SPACE_H):
        errors.append(f"{did}: space is {space.get('w')}x{space.get('h')}, not {SPACE_W}x{SPACE_H}")

    aperture = design.get("aperture")
    if aperture is not None:
        got = (aperture.get("x"), aperture.get("y"), aperture.get("w"),
               aperture.get("h"), aperture.get("r", 0))
        if got != APERTURE:
            errors.append(f"{did}: aperture {got} is not the Card's window {APERTURE}")

    inks = design.get("inks")
    if not isinstance(inks, dict):
        errors.append(f"{did}: no inks -- a design without colours is shapes only")
        inks = {}
    paper = _hex(inks.get("paper"), f"{did}: inks.paper", errors, allow_none=True)
    ink = _hex(inks.get("ink"), f"{did}: inks.ink", errors)
    rule = _hex(inks.get("rule"), f"{did}: inks.rule", errors)
    band = inks.get("band")
    if not isinstance(band, list) or not 4 <= len(band) <= BAND_MAX:
        errors.append(f"{did}: inks.band must be four to eight colours")
        band = ["#000000"] * 4
    band_declared = len(band)
    band = [_hex(c, f"{did}: inks.band[{i}]", errors) for i, c in enumerate(band)]
    # The sheet always has eight slots; a design that declares four leaves the rest 0.
    band += [0] * (BAND_MAX - len(band))

    fonts = design.get("fonts")
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

    # The canvas is optional: a design without one leaves the panel's page alone. When it is
    # there, only the active preset is packed; the rest are the editor's.
    canvas = design.get("canvas")
    canvas_flags = 0
    canvas_rgb = 0
    if canvas is not None:
        if not isinstance(canvas, dict):
            errors.append(f"{did}: canvas must be an object with backgrounds and active")
        else:
            backgrounds = canvas.get("backgrounds")
            if not isinstance(backgrounds, list) or not 1 <= len(backgrounds) <= CANVAS_MAX:
                errors.append(
                    f"{did}: canvas.backgrounds must be one to {CANVAS_MAX} colours")
                backgrounds = []
            parsed = [_hex(c, f"{did}: canvas.backgrounds[{i}]", errors)
                      for i, c in enumerate(backgrounds)]
            active = canvas.get("active")
            if (isinstance(active, bool) or not isinstance(active, int)
                    or not 0 <= active < len(parsed)):
                errors.append(
                    f"{did}: canvas.active {active!r} is not an index into backgrounds")
            elif parsed:
                canvas_flags = 1
                canvas_rgb = parsed[active]

    slots = design.get("slots") or {}
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
        # VALUE_MAX is LABEL_VALUE_MAX: a design cannot ask for more than the resolver carries,
        # so the view never has to cut a value the sheet allowed.
        slot_max[name] = _int((spec or {}).get("max"), f'{did}: slot "{name}".max',
                              errors, lo=1, hi=VALUE_MAX)

    prims = design.get("prims")
    if not isinstance(prims, list) or not prims:
        errors.append(f"{did}: no prims")
        prims = []
    if len(prims) > PRIM_MAX:
        errors.append(f"{did}: {len(prims)} prims, more than the {PRIM_MAX} allowed")

    out_prims = []
    used_slots = set()
    literals = []
    for i, prim in enumerate(prims, 1):
        tag = f"{did}: {_tag(prim, i)}"
        kind = prim.get("t")
        # x and y are int16 in the sheet, w and h uint16: a value the record cannot hold is a
        # refusal here, not a struct.error while packing
        x = _int(prim.get("x"), f"{tag}: x", errors, *COORD["x"])
        y = _int(prim.get("y"), f"{tag}: y", errors, *COORD["y"])
        w = _int(prim.get("w"), f"{tag}: w", errors, *COORD["w"])
        rot = _int(prim.get("rot", 0), f"{tag}: rot", errors, lo=-360, hi=360, default=0)
        # A prim may sit outside the print: the print crops it, and the editor marks what will
        # be cut (docs/specs/label-templates.md 1, ADR 0072). Position is not refused here; what
        # the sheet cannot hold - an int16 coordinate, a uint16 size - is refused above.
        if kind == "rect":
            h = _int(prim.get("h"), f"{tag}: h", errors, *COORD["h"])
            r = _int(prim.get("r", 0), f"{tag}: r", errors, lo=0, default=0)
            fill = prim.get("fill")
            literal = 0
            if fill in ROLE_INDEX:
                fill_index = ROLE_INDEX[fill]
                if fill.startswith("band.") and int(fill[5:]) >= band_declared:
                    errors.append(f"{tag}: fill {fill} is past this design's {band_declared} bands")
                if fill == "paper" and paper is None:
                    errors.append(f"{tag}: fills with paper, and this design has no paper colour")
            elif isinstance(fill, str) and HEX_RE.match(fill):
                fill_index = FILL_LITERAL
                literal = int(fill[1:], 16)
                literals.append((tag, design.get("notes") or []))
            else:
                errors.append(f"{tag}: fill {fill!r} is not a role and not #RRGGBB")
                fill_index = 0
            out_prims.append(struct.pack(
                "<BBBBhhHHHhII", 0, fill_index, 0, 0, x, y, w, h, r,
                round(rot * 10) % 3600, 0xFFFFFFFF, literal))
            continue

        if kind != "text":
            errors.append(f"{tag}: t must be rect or text")
            continue

        # A text prints in the ink or in one of the eight bands; the paper has nothing to read
        # against it, and the one literal colour is a rect's (label-templates.md 2).
        color = prim.get("color")
        if color not in TEXT_ROLES:
            errors.append(f"{tag}: colour {color!r} is not ink or a band")
            fill_index = ROLE_INDEX["ink"]
        else:
            fill_index = ROLE_INDEX[color]
            if color.startswith("band.") and int(color[5:]) >= band_declared:
                errors.append(f"{tag}: colour {color} is past this design's {band_declared} bands")
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
            "<BBBBhhHHHhII", kind_index, fill_index, font, align, x, y, w, 0, 0,
            round(rot * 10) % 3600, offset, 0))

    for name in slot_max:
        if name not in used_slots:
            errors.append(f'{did}: slot "{name}" is declared but no text uses it')
    if len(literals) > 1:
        errors.append(f"{did}: {len(literals)} layers use their own colour; a design may have one")
    if len(literals) == 1 and not any(str(n).strip() for n in literals[0][1]):
        errors.append(f"{did}: {literals[0][0]} uses its own colour and no note says why")

    if len(errors) > before:
        raise PackError(errors)

    slot_names = sorted(slot_max)
    slot_records = [struct.pack("<IB3x", pool.add(name), slot_max[name])
                    for name in slot_names]
    fields = {
        "name": pool.add(design.get("name") or design["id"]),
        "prim_count": len(out_prims),
        "slot_count": len(slot_records),
        "font": font_ids,
        "inks": (paper or 0, ink, rule, *band),
        "flags": canvas_flags,
        "canvas": canvas_rgb,
    }
    # The slot names travel back with the records, in the order they were packed, so the
    # manifest below is derived from the same list the sheet is - not from the design's
    # JSON key order, which is neither sorted nor what the reader will see.
    return fields, out_prims, slot_records, slot_names


def manifest_text(rows):
    """The sheet as its inputs describe it, for a reader to be checked against.

    Written beside the sheet and read by host/tests/t33_label_sheet.c, which opens the bytes
    with the reader the firmware ships and compares the two. It is a second opinion derived
    from the designs, not a re-reading of the blob: a manifest read back out of the sheet
    would agree with it whatever the packer did.

    Tab-separated: the counts, the canvas (`#RRGGBB`, or `-` when the design has none), the
    Slots, and the design's name last - so a name containing a tab only makes the name longer.
    The slots are comma-joined: a Slot's name is an identifier.
    """
    lines = ["label_manifest\t2", f"count\t{len(rows)}"]
    for i, (name, prim_count, slot_names, canvas) in enumerate(rows):
        lines.append(
            f"{i}\t{prim_count}\t{len(slot_names)}\t{canvas}\t{','.join(slot_names)}\t{name}")
    return "\n".join(lines) + "\n"


def pack_sheet(designs, fonts):
    """Every design -> the sheet's bytes, the font header's text, and the manifest.

    Raises PackError."""
    errors = []
    if not designs:
        raise PackError([f"no designs: assets/labels/{TEMPLATES}/ has no *.json to pack"])
    if len(designs) > TEMPLATE_MAX:
        raise PackError([f"{len(designs)} designs, more than the {TEMPLATE_MAX} a sheet holds"])

    font_index = {(f["name"], f["px"]): i for i, f in enumerate(fonts)}
    pool = Pool()
    ids = set()
    ordered = sorted(designs, key=lambda d: str(d.get("id")))
    for design in ordered:
        did = design.get("id")
        if not isinstance(did, str) or not ID_RE.match(did):
            errors.append(f'{did}: id must look like label.my-name')
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
            design.get("name") or design["id"],
            len(tp),
            slot_names,
            f"#{fields['canvas']:06X}" if fields["flags"] else "-",
        ))

    if errors:
        raise PackError(errors)

    pool_bytes = pool.bytes()
    total = (HEADER_SIZE + TEMPLATE_SIZE * len(templates) + PRIM_SIZE * len(prims)
             + SLOT_SIZE * len(slots) + len(pool_bytes))
    body = bytearray()
    for t in templates:
        # 16 B of name/counts/fonts/flags, the eleven role colours (paper, ink, rule, eight
        # bands), then the active canvas: 64 B.
        body += struct.pack(
            "<IHHHHBBBB11II",
            t["name"], t["prim_start"], t["prim_count"], t["slot_start"], t["slot_count"],
            t["font"][0], t["font"][1], t["font"][2], t["flags"],
            *t["inks"], t["canvas"])
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
    want = HEADER_SIZE + TEMPLATE_SIZE * templates + PRIM_SIZE * prims + SLOT_SIZE * slots + pool_size
    if total != want or total != len(blob):
        errors.append(f"total_size {total} disagrees with the parts ({want}) and the file ({len(blob)})")
    if (sw, sh) != (SPACE_W, SPACE_H):
        errors.append(f"space is {sw}x{sh}, not {SPACE_W}x{SPACE_H}")
    if not 1 <= templates <= TEMPLATE_MAX:
        errors.append(f"{templates} templates")
    if crc != zlib.crc32(blob[HEADER_SIZE:]):
        errors.append("crc32 disagrees with the body")
    pool_start = total - pool_size
    for i in range(templates):
        (_, p_start, p_count, s_start, s_count, f0, f1, f2, flags,
         *rest) = struct.unpack_from("<IHHHHBBBB11II", blob, HEADER_SIZE + i * TEMPLATE_SIZE)
        canvas = rest[11]
        if p_start + p_count > prims or s_start + s_count > slots:
            errors.append(f"template {i} runs past its arrays")
        if flags & 0xFE:
            errors.append(f"template {i}: flags {flags} are not has_canvas and zero")
        if flags & 1:
            if canvas > 0xFFFFFF:
                errors.append(f"template {i}: canvas is above RGB888")
        elif canvas != 0:
            errors.append(f"template {i}: has no canvas but carries a colour")
    for i in range(prims):
        off = HEADER_SIZE + TEMPLATE_SIZE * templates + i * PRIM_SIZE
        kind, fill, font, align, x, y, w, h, r, rot, text_off, _ = struct.unpack_from(
            "<BBBBhhHHHhII", blob, off)
        if kind == 0 and text_off != 0xFFFFFFFF:
            errors.append(f"prim {i}: a rect carries a text offset")
        if kind != 0:
            if not 0 <= text_off < pool_size:
                errors.append(f"prim {i}: text offset {text_off} is outside the pool")
            elif blob.find(b"\0", pool_start + text_off) < 0:
                errors.append(f"prim {i}: text is not NUL-terminated")
        if fill > FILL_LITERAL or font > 2 or align > 2:
            errors.append(f"prim {i}: kind/fill/font/align out of range")
    if errors:
        raise PackError(errors)
    return (f"{templates} designs, {prims} prims, {slots} slots, {pool_size} B pool, "
            f"{len(blob)} B, crc 0x{crc:08X}")


def load_fonts(src):
    """The sheet's font table: the faces the build compiles, in index order.

    The table lives in the same vocabulary file as the Slots and the limits, because a face
    that the editor offers and the build cannot compile is exactly the drift it exists to
    stop. `editor: true` marks the faces the editor offers and can draw.
    """
    path = Path(src) / VOCABULARY
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
    """Every *.json in <src>/templates is a design; a file that is not one is an error.

    Nothing is skipped silently: a design left in the folder and not packed would be a Track
    that prints the wrong label and no line saying why. A name starting with a dot is not a
    design (the Export's zip, a .DS_Store or an AppleDouble sidecar), which is the one thing
    ignored, and it is ignored by name rather than by guessing.
    """
    root = Path(src) / TEMPLATES
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
            errors.append(f"{path}: not a JSON object, so not a design")
            continue
        if "templates" in design and "prims" not in design:
            errors.append(f"{path}: that is the Export's labels.json manifest, not a design; "
                          f"copy its label.*.json files into {root}")
            continue
        if not all(k in design for k in ("v", "id", "space", "prims")):
            errors.append(f"{path}: not a label-template v1 design (it needs v, id, space and "
                          f"prims; the editor's Export writes one, its Save writes a document)")
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
        print("usage: label_pack.py <assets/labels> <out-dir> [--check]", file=sys.stderr)
        return 2
    src, out = Path(args[0]), Path(args[1])
    try:
        fonts = load_fonts(src)
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
    if v.get("v") != 1:
        errors.append(f"{VOCABULARY}: v is {v.get('v')}, not 1")

    space = v.get("space") or {}
    if not isinstance(space.get("w"), int) or not isinstance(space.get("h"), int):
        errors.append(f"{VOCABULARY}: space needs integer w and h")

    bands = v.get("bands")
    if not isinstance(bands, int) or not 4 <= bands <= 8:
        errors.append(f"{VOCABULARY}: bands is {bands!r}; a design declares four to eight")

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
        f"#define LABEL_BAND_COUNT    {v['bands']}u",
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
        f"  bands: {v['bands']},",
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
        "v": 1, "id": "label.self", "name": "Self", "space": {"w": 748, "h": 359},
        "aperture": {"x": 95, "y": 101, "w": 556, "h": 166, "r": 83},
        "canvas": {"backgrounds": ["#101010", "#F5F1E6"], "active": 1},
        "inks": {"paper": "#F2EDE3", "ink": "#24211D", "rule": "#B9AC8E",
                 "band": ["#C68B2E", "#A24E2C", "#8A5D34", "#6B7A2F",
                          "#3B6E8F", "#7A3B52", "#B9AC8E", "#2F4B3A"]},
        "fonts": {"title": {"ff": "Archivo Narrow 600", "px": 32},
                  "numeral": {"ff": "Jura", "px": 20},
                  "caption": {"ff": "Jura", "px": 20}},
        "slots": {"song_title": {"max": 24}},
        "prims": [
            {"t": "rect", "x": 0, "y": 0, "w": 748, "h": 359, "r": 19, "fill": "paper"},
            {"t": "rect", "x": 0, "y": 300, "w": 40, "h": 20, "r": 0, "fill": "#C63A2E"},
            {"t": "text", "slot": "song_title", "x": 32, "y": 280, "w": 400,
             "font": "title", "color": "ink", "align": "left", "preview": "Neon Skyline"},
            {"t": "text", "text": "SIDE A", "x": 32, "y": 20, "w": 200,
             "font": "caption", "color": "ink", "align": "right"},
        ],
        "notes": ["the mark is the one literal colour"],
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
    assert "1 designs, 4 prims, 1 slots" in summary, summary
    assert "LABEL_FONT_JURA_20 0" in header and "LABEL_FONT_COUNT 2" in header
    # The manifest is what t33 compares the opened sheet against, so its shape is part of
    # the packer's contract: index, prim count, slot count, canvas, the packed slot names,
    # the name.
    assert manifest == ("label_manifest\t2\ncount\t1\n"
                        "0\t4\t1\t#F5F1E6\tsong_title\tSelf\n"), repr(manifest)
    # Version 3: the active preset is what the record carries, beside the flags byte.
    fields = struct.unpack_from("<IHHHHBBBB11II", blob, HEADER_SIZE)
    assert fields[8] == 1 and fields[-1] == 0xF5F1E6, fields
    no_canvas = json.loads(json.dumps(good))
    del no_canvas["canvas"]
    bare, _, bare_manifest = pack_sheet([no_canvas], fonts)
    assert verify_sheet(bare)
    fields = struct.unpack_from("<IHHHHBBBB11II", bare, HEADER_SIZE)
    assert fields[8] == 0 and fields[-1] == 0, fields
    assert bare_manifest.endswith("0\t4\t1\t-\tsong_title\tSelf\n"), bare_manifest

    # a folder is the set: every *.json in templates/ is a design, a stray file fails naming
    # itself, the Export's manifest says what it is, and a dot-name is not a design
    with tempfile.TemporaryDirectory() as d:
        templates = Path(d) / TEMPLATES
        templates.mkdir()
        try:
            load_designs(d)
            raise SystemExit("selftest: an empty templates/ was accepted")
        except PackError as exc:
            assert "no *.json" in exc.errors[0], exc.errors

        (templates / "label.self.json").write_text(json.dumps(good))
        (templates / ".DS_Store").write_text("junk")
        (templates / "._label.self.json").write_text("junk")
        assert [x["id"] for x in load_designs(d)] == ["label.self"], "the dot-names were read"

        (templates / "labels.json").write_text(json.dumps({"v": 1, "templates": []}))
        try:
            load_designs(d)
            raise SystemExit("selftest: the Export's manifest was accepted as a design")
        except PackError as exc:
            assert "labels.json manifest" in exc.errors[0], exc.errors

        (templates / "labels.json").unlink()
        (templates / "label.stray.json").write_text(json.dumps({"hello": 1}))
        try:
            load_designs(d)
            raise SystemExit("selftest: a file that is not a design was accepted")
        except PackError as exc:
            assert "not a label-template v1 design" in exc.errors[0], exc.errors

    expect_refusal(lambda d: d.update(space={"w": 800, "h": 359}), "space is 800x359")
    # a design may bleed past the print; the print crops it (ADR 0072)
    bleed = json.loads(json.dumps(good))
    bleed["prims"][0].update(x=400)
    pack_sheet([bleed], fonts)
    expect_refusal(lambda d: d["prims"][2].update(font="numeral2"), "font 'numeral2'")
    expect_refusal(lambda d: d["prims"][2].update(slot="year"), "not a slot in the vocabulary")
    expect_refusal(lambda d: d["fonts"].update(title={"ff": "Nope", "px": 12}), "does not carry")
    expect_refusal(lambda d: d.update(notes=[]), "no note says why")
    expect_refusal(lambda d: d["prims"].append({"t": "rect", "x": 0, "y": 0, "w": 4, "h": 4,
                                               "fill": "#111111"}), "may have one")
    expect_refusal(lambda d: d["inks"].update(band=["#000000"] * 3), "four to eight")
    expect_refusal(lambda d: d["inks"].update(band=["#000000"] * 9), "four to eight")
    expect_refusal(lambda d: d.update(canvas={"backgrounds": [], "active": 0}),
                   "canvas.backgrounds must be one to 8")
    expect_refusal(lambda d: d.update(canvas={"backgrounds": ["#101010"] * 9, "active": 0}),
                   "canvas.backgrounds must be one to 8")
    expect_refusal(lambda d: d.update(canvas={"backgrounds": ["#nope"], "active": 0}),
                   "canvas.backgrounds[0]")
    expect_refusal(lambda d: d.update(canvas={"backgrounds": ["#101010"], "active": 2}),
                   "canvas.active 2")

    # A design may keep four bands; a prim may not name a band it did not declare.
    short = json.loads(json.dumps(good))
    short["inks"]["band"] = short["inks"]["band"][:4]
    short["prims"][0]["fill"] = "band.4"
    try:
        pack_sheet([short], fonts)
        raise SystemExit("selftest: an undeclared band was accepted")
    except PackError as exc:
        if not any("past this design's 4 bands" in e for e in exc.errors):
            raise SystemExit(f"selftest: expected the undeclared band, got {exc.errors}")
    print("label_pack: selftest ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
