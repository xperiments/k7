#!/usr/bin/env python3
"""Pack `label-template v1` designs into one label sheet.

`docs/specs/label-templates.md` is the contract: §2 is the design file this reads, §3 is the
sheet it writes, §7 is the list of things it refuses. It refuses rather than substitutes -- a
design that does not fit the panel is a build failure, not a design that quietly prints
something else.

    python3 tools/label_pack.py assets/labels build/      # write the sheet and label_fonts.h
                                                          # (reads templates/*.json and fonts.json)
    python3 tools/label_pack.py assets/labels build/ --check   # validate, write, then re-read
    python3 tools/label_pack.py --selftest
    python3 tools/label_pack.py --check-editor   # the editor's tables against this file's

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
VERSION = 1
HEADER_SIZE = 32
TEMPLATE_SIZE = 48
PRIM_SIZE = 24
SLOT_SIZE = 8

SPACE_W, SPACE_H = 748, 359
# The Card's window in the design's own space: (95,101) 556x166, radius 83.
APERTURE = (95, 101, 556, 166, 83)

# The designs sit in their own folder: assets/labels/ also holds the K7 art (k7logo.png,
# cover.jpg), and a sheet is packed from templates/ alone. Every *.json in it is a design -
# nothing is skipped silently - and fonts.json stays at the root because it is the sheet's font
# table, not a design.
TEMPLATES = "templates"

PRIM_MAX = 48
SLOT_MAX = 16
TEMPLATE_MAX = 32

ROLE_INDEX = {"paper": 0, "ink": 1, "rule": 2, "band.0": 3, "band.1": 4, "band.2": 5, "band.3": 6}
FILL_LITERAL = 7
FONT_ROLES = ("title", "numeral", "caption")
ALIGN_INDEX = {"left": 0, "center": 1, "right": 2}

# The device's vocabulary: one table, shared with the editor's SLOTS and the reader.
SLOT_NAMES = {
    "song_title", "song_artist", "song_album", "genre", "bpm", "musical_key",
    "energy", "danceability", "valence", "arousal", "mood",
}

ID_RE = re.compile(r"^label\.[a-z0-9][a-z0-9-]*$")
HEX_RE = re.compile(r"^#[0-9A-Fa-f]{6}$")

ROOT = Path(__file__).resolve().parent.parent
EDITOR = ROOT / "resources" / "LVGL canvas builder.html"


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

    if design.get("v") != VERSION:
        errors.append(f"{did}: v is not {VERSION}")

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
    if not isinstance(band, list) or len(band) != 4:
        errors.append(f"{did}: inks.band must be four colours")
        band = ["#000000"] * 4
    band = [_hex(c, f"{did}: inks.band[{i}]", errors) for i, c in enumerate(band)]

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
                f"which assets/labels/fonts.json does not carry")
            font_ids.append(0)
        else:
            font_ids.append(font_index[key])

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
        # 96 is LABEL_VALUE_MAX: a design cannot ask for more than the resolver carries, so
        # the view never has to cut a value the sheet allowed.
        slot_max[name] = _int((spec or {}).get("max"), f'{did}: slot "{name}".max',
                              errors, lo=1, hi=96)

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
        x = _int(prim.get("x"), f"{tag}: x", errors)
        y = _int(prim.get("y"), f"{tag}: y", errors)
        w = _int(prim.get("w"), f"{tag}: w", errors, lo=1)
        rot = _int(prim.get("rot", 0), f"{tag}: rot", errors, lo=-360, hi=360, default=0)
        if x < 0 or y < 0 or x + w > SPACE_W or y >= SPACE_H:
            errors.append(f"{tag}: outside the {SPACE_W}x{SPACE_H} print, which clips it")

        if kind == "rect":
            h = _int(prim.get("h"), f"{tag}: h", errors, lo=1)
            r = _int(prim.get("r", 0), f"{tag}: r", errors, lo=0, default=0)
            if y + h > SPACE_H:
                errors.append(f"{tag}: outside the {SPACE_W}x{SPACE_H} print, which clips it")
            fill = prim.get("fill")
            literal = 0
            if fill in ROLE_INDEX:
                fill_index = ROLE_INDEX[fill]
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

        if prim.get("color") != "ink":
            errors.append(f"{tag}: colour {prim.get('color')!r} is not the ink role")
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
                preview = prim.get("preview") or ""
                if len(preview) > slot_max[name]:
                    errors.append(
                        f'{tag}: the preview "{preview}" is longer than its slot\'s '
                        f"{slot_max[name]} characters")
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
            "<BBBBhhHHHhII", kind_index, 1, font, align, x, y, w, 0, 0,
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

    slot_records = [struct.pack("<IB3x", pool.add(name), slot_max[name])
                    for name in sorted(slot_max)]
    fields = {
        "name": pool.add(design.get("name") or design["id"]),
        "prim_count": len(out_prims),
        "slot_count": len(slot_records),
        "font": font_ids,
        "inks": (paper or 0, ink, rule, *band),
    }
    return fields, out_prims, slot_records


def pack_sheet(designs, fonts):
    """Every design -> the sheet's bytes and the font header's text. Raises PackError."""
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

    templates, prims, slots = [], [], []
    for design in ordered:
        try:
            fields, tp, ts = pack_design(design, font_index, pool)
        except PackError as exc:
            errors.extend(exc.errors)
            continue
        fields["prim_start"] = len(prims)
        fields["slot_start"] = len(slots)
        prims.extend(tp)
        slots.extend(ts)
        templates.append(fields)

    if errors:
        raise PackError(errors)

    pool_bytes = pool.bytes()
    total = (HEADER_SIZE + TEMPLATE_SIZE * len(templates) + PRIM_SIZE * len(prims)
             + SLOT_SIZE * len(slots) + len(pool_bytes))
    body = bytearray()
    for t in templates:
        body += struct.pack(
            "<IHHHHBBBBIIIIIIII",
            t["name"], t["prim_start"], t["prim_count"], t["slot_start"], t["slot_count"],
            t["font"][0], t["font"][1], t["font"][2], 0,
            t["inks"][0], t["inks"][1], t["inks"][2], t["inks"][3], t["inks"][4],
            t["inks"][5], t["inks"][6], 0)
    body += b"".join(prims)
    body += b"".join(slots)
    body += pool_bytes

    header = struct.pack(
        "<8sHHIIHHHHHH", MAGIC, VERSION, HEADER_SIZE, total, zlib.crc32(bytes(body)),
        SPACE_W, SPACE_H, len(templates), len(prims), len(slots), len(pool_bytes))
    assert len(header) == HEADER_SIZE and len(body) == total - HEADER_SIZE

    font_header = (
        "/* Generated by tools/label_pack.py from assets/labels/fonts.json. Do not edit.\n"
        " * The sheet names a font by its index here; the app's k_label_font[] is this table. */\n"
        "#pragma once\n\n"
        + "".join(f"#define LABEL_FONT_{f['symbol'].upper()} {i}\n"
                  for i, f in enumerate(fonts))
        + f"\n#define LABEL_FONT_COUNT {len(fonts)}\n")
    return bytes(header) + bytes(body), font_header


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
    if version != VERSION:
        errors.append(f"version is {version}, not {VERSION}")
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
        (_, p_start, p_count, s_start, s_count, *_) = struct.unpack_from(
            "<IHHHHBBBBIIIIIIII", blob, HEADER_SIZE + i * TEMPLATE_SIZE)
        if p_start + p_count > prims or s_start + s_count > slots:
            errors.append(f"template {i} runs past its arrays")
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
    path = Path(src) / "fonts.json"
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

    if "--check-editor" in flags:
        try:
            errors = check_editor()
        except PackError as exc:
            errors = exc.errors
        for line in errors:
            print(f"label_pack: {line}", file=sys.stderr)
        if errors:
            return 1
        print("label_pack: the editor offers the sheet's faces and the vocabulary's slots")
        return 0

    if len(args) != 2:
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        print("usage: label_pack.py <assets/labels> <out-dir> [--check]", file=sys.stderr)
        return 2
    src, out = Path(args[0]), Path(args[1])
    try:
        fonts = load_fonts(src)
        designs = load_designs(src)
        blob, font_header = pack_sheet(designs, fonts)
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
    return 0


def check_editor():
    """The editor's tables are this file's, and a drift between them is silent.

    `resources/LVGL canvas builder.html` is a standalone page, so it repeats the Slot
    vocabulary, the faces the sheet carries and the three role defaults
    (docs/specs/label-templates.md §2). When they drift, Export succeeds and the build refuses
    the file - or the page offers a field the Snapshot cannot fill, and the design prints
    nothing. Neither is visible until a design is packed, so the packer checks the page with
    its own tables: `make -C host check` runs this.
    """
    html = EDITOR.read_text(encoding="utf-8")
    errors = []

    carried = {(f["name"], f["px"]) for f in load_fonts(ROOT / "assets" / "labels")}
    # the editor is a hand-formatted page and gets reformatted; the tables are found by their
    # declaration, not by their layout, so a reindent or a change of quote style cannot hide them
    m = re.search(r"SHEET_FONTS\s*=\s*\[(.*?)\]\s*;", html, re.S)
    if not m:
        return [f"{EDITOR}: no SHEET_FONTS table"]
    pairs = [(name, int(px)) for name, px in
             re.findall(r"\[\s*['\"]([^'\"]+)['\"]\s*,\s*(\d+)\s*\]", m.group(1))]
    if not pairs:
        errors.append(f"{EDITOR}: SHEET_FONTS is empty")
    for pair in pairs:
        if pair not in carried:
            errors.append(f"{EDITOR}: offers {pair[0]!r} at {pair[1]} px, "
                          f"which assets/labels/fonts.json does not carry")

    d = re.search(r"function\s+defaultTpl\s*\(\s*\)\s*\{.*?fonts:\s*\{(.*?)\}\s*,\s*slots", html, re.S)
    if not d:
        errors.append(f"{EDITOR}: no defaultTpl font roles")
    else:
        for role, name, px in re.findall(
                r"(\w+)\s*:\s*\{\s*ff\s*:\s*['\"]([^'\"]+)['\"]\s*,\s*px\s*:\s*(\d+)",
                d.group(1)):
            if (name, int(px)) not in pairs:
                errors.append(f"{EDITOR}: defaults the {role} role to {name!r} at {px} px, "
                              f"which it does not offer")

    m = re.search(r"SLOTS\s*=\s*\[(.*?)\]\s*;", html, re.S)
    if not m:
        return errors + [f"{EDITOR}: no SLOTS table"]
    offered = re.findall(r"id\s*:\s*['\"]([^'\"]+)['\"]", m.group(1))
    for name in offered:
        if name not in SLOT_NAMES:
            errors.append(f'{EDITOR}: offers the slot "{name}", which the vocabulary does not carry')
    for name in sorted(SLOT_NAMES - set(offered)):
        errors.append(f'{EDITOR}: is missing the slot "{name}"')
    return errors


def selftest():
    fonts = [{"name": "Jura", "px": 20, "symbol": "jura_20"},
             {"name": "Archivo Narrow 600", "px": 24, "symbol": "archivo_narrow_600_24"}]
    good = {
        "v": 1, "id": "label.self", "name": "Self", "space": {"w": 748, "h": 359},
        "aperture": {"x": 95, "y": 101, "w": 556, "h": 166, "r": 83},
        "inks": {"paper": "#F2EDE3", "ink": "#24211D", "rule": "#B9AC8E",
                 "band": ["#C68B2E", "#A24E2C", "#8A5D34", "#6B7A2F"]},
        "fonts": {"title": {"ff": "Archivo Narrow 600", "px": 24},
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

    blob, header = pack_sheet([good], fonts)
    summary = verify_sheet(blob)
    assert "1 designs, 4 prims, 1 slots" in summary, summary
    assert "LABEL_FONT_JURA_20 0" in header and "LABEL_FONT_COUNT 2" in header

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
    expect_refusal(lambda d: d["prims"][0].update(x=400), "outside the 748x359 print")
    expect_refusal(lambda d: d["prims"][2].update(font="numeral2"), "font 'numeral2'")
    expect_refusal(lambda d: d["prims"][2].update(slot="year"), "not a slot in the vocabulary")
    expect_refusal(lambda d: d["fonts"].update(title={"ff": "Nope", "px": 12}), "does not carry")
    expect_refusal(lambda d: d.update(notes=[]), "no note says why")
    expect_refusal(lambda d: d["prims"].append({"t": "rect", "x": 0, "y": 0, "w": 4, "h": 4,
                                               "fill": "#111111"}), "may have one")
    print("label_pack: selftest ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
