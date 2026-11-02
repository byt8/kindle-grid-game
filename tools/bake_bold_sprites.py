#!/usr/bin/env python3
"""Bake sprites for the Kindle panel and write src/sprites.h.

The art is drawn at 512x512 per sprite and the panel does grey, so the
tone is kept: crop to the bounding box, area-resize into a 36x34 box, and
store what comes out. The picture is never thresholded -- at 40x40 a hard
threshold throws away exactly the information e-ink can show, which is why
the old version of this file's output read as flat silhouettes.

Two decisions do still need a binary image, and both are explicit:

  - the silhouette mask. Transparency has no grey: a pixel is either on
    the tile or it is background. A flood-fill from the border over the
    thresholded resize decides which it is.
  - the ink guard. A sprite that comes out too faint stops the run.

A tile pixel is 0 transparent, 1 paper, 2..9 a tone where v-1 is the ink
level 0..8. xcom.c scales that level with its ink15() helper and hands it
to ek_grey_rect(), the engine's greyscale layer, so the tone stored here
is the tone the panel draws -- no dither sits between the two.

Terrain is not taken from a picture. A photo of grass scaled to 40x40 is
noise; a 40x40 hatch reads as "grass" across the room. So the terrain
fills come from PATTERNS below, keyed by terrain id. Adding a terrain id
without a pattern is an error, not a silent white tile.

Anything declared in the manifest but absent from the sheet, or present
with no ink, stops the run. Nothing is substituted.
"""
import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "bold_sprite_sources.json"
STATS = ROOT / "tactical_sprite_map_with_descriptions.json"
OUT = ROOT / "src" / "sprites.h"

TILE = 40
# The figure is fitted inside this box, leaving a margin so the panel's
# own rim and the HP bar do not sit on top of the silhouette.
FIT_W, FIT_H = 36, 34

# Tile pixel values, packed 4 bits each by pack() below.
#   0          transparent: no pixel of the tile is written at all
#   1          paper: an opaque white pixel (the unit's body highlight)
#   2..9       a tone, where v - 1 is the ink level 0..8
# 9 is solid ink, so INK is what every terrain pattern writes.
TRANSPARENT, PAPER = 0, 1
BAYER_LEVELS = 8
TONE_MAX = PAPER + BAYER_LEVELS      # 9
INK = TONE_MAX

# The ramp eink.c's grey layer displays, plus the ink15() mapping xcom.c
# uses to put an ink level 0..8 on it. Mirrored here so a preview shows
# the panel's grey instead of a guess at it. If the grey layer cannot be
# allocated the engine falls back to a 4x4 Bayer dither of level*2, which
# is byte for byte the look this game had before greyscale.
GREY_RAMP = (255, 238, 221, 204, 187, 170, 153, 136,
             119, 102, 85, 68, 51, 34, 17, 0)


def ink15(level):
    """Sprite ink level 0..8 -> grey layer level 0..15, same rounding as xcom."""
    return (level * 15 + 4) // 8

# Ascii ramp for --ascii: indexed by the tile pixel value itself, so
# 0 (transparent) is blank and 9 (solid ink) is the darkest glyph.
ASCII = " .:-=+*#%@"

# Threshold, used only where a decision has to be binary: the bounding
# box, the silhouette mask, and the guard below. The picture itself is
# never thresholded. Slightly below mid-grey keeps the model's
# anti-aliased edge pixels in the silhouette.
THRESH = 120


def die(msg):
    print("bake_bold_sprites: " + msg, file=sys.stderr)
    raise SystemExit(1)


def load_cell(path, cols, rows, index):
    """Grey levels of one cell of a generated sheet.

    The image tool writes WebP bytes into a .png file, so the format is
    read by sniffing, never by the extension. Pillow is a build-time tool
    dependency only; nothing here ships to the Kindle.
    """
    if not path.exists():
        die(f"missing sheet {path}")
    try:
        img = Image.open(path)
    except OSError as e:
        die(f"cannot read {path}: {e}")
    w, h = img.size
    cw, ch = w // cols, h // rows
    if cw * cols != w or ch * rows != h:
        die(f"{path} is {w}x{h}, does not divide into a {cols}x{rows} grid")
    box = ((index % cols) * cw, (index // cols) * ch, (index % cols) * cw + cw,
           (index // cols) * ch + ch)
    cell = img.convert("L").crop(box)
    return bytearray(cell.tobytes()), cell.size[0], cell.size[1]


def threshold(g, n):
    return bytearray(1 if v < THRESH else 0 for v in g[:n])


def outside_white(ink, w, h):
    """Flood fill white from the border. Everything white and reachable
    from the edge is background; white that the ink encloses is body."""
    seen = bytearray(w * h)
    stack = []
    for x in range(w):
        stack.append((x, 0))
        stack.append((x, h - 1))
    for y in range(h):
        stack.append((0, y))
        stack.append((w - 1, y))
    while stack:
        x, y = stack.pop()
        i = y * w + x
        if seen[i] or ink[i]:
            continue
        seen[i] = 1
        if x > 0:
            stack.append((x - 1, y))
        if x + 1 < w:
            stack.append((x + 1, y))
        if y > 0:
            stack.append((x, y - 1))
        if y + 1 < h:
            stack.append((x, y + 1))
    return seen


def shape_mask(ink, w, h):
    out = outside_white(ink, w, h)
    return bytearray(0 if out[i] else 1 for i in range(w * h))


def bbox(mask, w, h):
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = y * w
        for x in range(w):
            if mask[row + x]:
                if x < x0:
                    x0 = x
                if y < y0:
                    y0 = y
                if x > x1:
                    x1 = x
                if y > y1:
                    y1 = y
    if x1 < x0:
        return None
    return x0, y0, x1 - x0 + 1, y1 - y0 + 1


def area_resize(g, w, h, dw, dh):
    out = bytearray(dw * dh)
    for y in range(dh):
        y0 = y * h / dh
        y1 = (y + 1) * h / dh
        iy0, iy1 = int(y0), max(int(y1), int(y0) + 1)
        if iy1 > h:
            iy1 = h
        for x in range(dw):
            x0 = x * w / dw
            x1 = (x + 1) * w / dw
            ix0, ix1 = int(x0), max(int(x1), int(x0) + 1)
            if ix1 > w:
                ix1 = w
            acc = 0
            n = 0
            for yy in range(iy0, iy1):
                base = yy * w
                for xx in range(ix0, ix1):
                    acc += g[base + xx]
                    n += 1
            if n == 0:
                die("resize box was empty")
            out[y * dw + x] = acc // n
    return out


def dilate(ink, w, h, passes):
    """Grow ink by `passes` pixels (4-neighbour). Guarantees no stroke ends
    up thinner than the outline the eye needs on a 40 px tile."""
    cur = ink
    for _ in range(passes):
        out = bytearray(cur)
        for y in range(h):
            for x in range(w):
                if cur[y * w + x]:
                    continue
                if (x > 0 and cur[y * w + x - 1]) or (x + 1 < w and cur[y * w + x + 1]) \
                        or (y > 0 and cur[(y - 1) * w + x]) or (y + 1 < h and cur[(y + 1) * w + x]):
                    out[y * w + x] = 1
        cur = out
    return cur


def cell_to_tile(g, cw, ch, trace=None):
    """One 512x512 cell of generated art -> TILE*TILE values of 0..9.

    The tone comes from `small`, the area-resized greyscale, straight
    through. The only threshold on the path decides what is background,
    because transparency is not a shade: outside the silhouette no pixel
    of the tile exists, inside it every shade survives.

    `trace`, when it is a dict, receives every intermediate stage as
    (bytes, width, height) under its step name, so a diagnostic can show
    exactly what the panel gets. It cannot change the tile produced.
    """

    def note(key, val, w=None, h=None):
        if trace is not None:
            trace[key] = (bytearray(val), w, h)

    note("src", g, cw, ch)
    ink = threshold(g, cw * ch)
    if sum(ink) == 0:
        return None
    # A cell drawn in white-on-black has more ink than paper. Flip it so the
    # silhouette is always the darker side.
    flip = sum(ink) * 2 > cw * ch
    if flip:
        ink = bytearray(0 if v else 1 for v in ink)
    note("src_ink", ink, cw, ch)
    box = bbox(ink, cw, ch)
    if box is None:
        return None
    bx, by, bw, bh = box
    s = min(FIT_W / bw, FIT_H / bh)
    dw = max(1, min(FIT_W, int(round(bw * s))))
    dh = max(1, min(FIT_H, int(round(bh * s))))
    cut = bytearray(bw * bh)
    for y in range(bh):
        src = (by + y) * cw + bx
        cut[y * bw:(y + 1) * bw] = g[src:src + bw]
    if flip:
        # White-on-black art: the bbox came from the flipped mask, so the
        # greyscale that feeds the tone has to be flipped with it.
        cut = bytearray(255 - v for v in cut)
    small = area_resize(cut, bw, bh, dw, dh)
    note("small", small, dw, dh)
    small_ink = threshold(small, dw * dh)
    if sum(small_ink) == 0:
        die("art vanished when scaled to 40 px")
    note("small_ink", small_ink, dw, dh)
    # The mask is closed by one dilation before the flood runs. At 36 px a
    # single gap in an outline would let the flood from the border into the
    # body and punch holes in the silhouette. It touches nothing else: the
    # tone below still comes from the untouched resize.
    mask = shape_mask(dilate(small_ink, dw, dh, 1), dw, dh)
    note("mask", mask, dw, dh)

    tile = bytearray(TILE * TILE)
    ox = (TILE - dw) // 2
    oy = (TILE - dh) // 2
    for y in range(dh):
        for x in range(dw):
            i = y * dw + x
            if not mask[i]:
                continue
            # The average dark of this pixel of the resize, as an ink level.
            level = (255 - small[i]) * BAYER_LEVELS // 255
            tile[(oy + y) * TILE + ox + x] = PAPER + level
    note("tile", tile, TILE, TILE)
    return tile


# --------------------------------------------------------------------------
# Terrain: bold patterns, not pictures. Each is a decision you can name out
# loud at a glance: dots = grass, trunks = forest, waves = water, dashes =
# road, windows = city, plus-shapes = rubble, peaks = mountains.
# --------------------------------------------------------------------------

def t_grass():
    """Light dotted ground. Sparse so units on it stay readable."""
    px = bytearray(TILE * TILE)
    for y in range(TILE):
        for x in range(TILE):
            if x % 8 == 3 and y % 8 == 3:
                px[y * TILE + x] = INK
            elif x % 8 == 7 and y % 8 == 7:
                px[y * TILE + x] = INK
            else:
                px[y * TILE + x] = PAPER
    return px


def t_forest():
    """Vertical trunks with a canopy band: dense and unmistakable."""
    px = bytearray(TILE * TILE)
    for y in range(TILE):
        for x in range(TILE):
            trunk = (x % 10) in (2, 3, 4)
            canopy = 6 <= (y % 16) <= 10 and (x % 10) <= 7
            px[y * TILE + x] = INK if (trunk or canopy) else PAPER
    return px


def t_water():
    """Wavy horizontal lines: water, and never confused with flat ground."""
    px = bytearray(TILE * TILE)
    for y in range(TILE):
        for x in range(TILE):
            wave = (y + (3 if (x // 5) % 2 else 0)) % 8
            px[y * TILE + x] = INK if wave == 0 else PAPER
    return px


def t_road():
    """Bold centre dashes on a plain bed."""
    px = bytearray(TILE * TILE)
    for y in range(TILE):
        for x in range(TILE):
            if 3 <= x <= 36 and (x in (2, 3, 36, 37) or y in (2, 3, 36, 37)):
                px[y * TILE + x] = INK
            elif 18 <= x <= 21 and (y % 14) < 7:
                px[y * TILE + x] = INK
            else:
                px[y * TILE + x] = PAPER
    return px


def t_city():
    """Window grid: a built-up block."""
    px = bytearray(TILE * TILE)
    for y in range(TILE):
        for x in range(TILE):
            px[y * TILE + x] = INK if (x % 10 in (5, 6) and y % 10 in (5, 6)) else PAPER
    return px


def t_rubble():
    """Scattered plus shapes, chunkier than grass dots."""
    px = bytearray(TILE * TILE)
    spots = ((6, 7), (19, 4), (31, 11), (11, 20), (25, 23), (5, 32), (33, 30), (18, 34))
    for y in range(TILE):
        for x in range(TILE):
            px[y * TILE + x] = PAPER
    for sx, sy in spots:
        for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (2, 0), (-2, 0)):
            x, y = sx + dx, sy + dy
            if 0 <= x < TILE and 0 <= y < TILE:
                px[y * TILE + x] = INK
    return px


def t_mountains():
    """Two heavy peaks. The one tile you can spot from across the panel."""
    px = bytearray(TILE * TILE)
    for y in range(TILE):
        for x in range(TILE):
            px[y * TILE + x] = PAPER
    for peak_x, peak_h, half in ((13, 30, 16), (30, 22, 12)):
        for y in range(peak_h):
            w = half * (peak_h - y) // peak_h
            for dx in range(-w, w + 1):
                x = peak_x + dx
                if 0 <= x < TILE:
                    px[y * TILE + x] = INK
        # A white snow notch so the peak shape reads even when small.
        for y in range(0, peak_h // 3):
            w = max(0, (half * (peak_h - y) // peak_h) - 3)
            for dx in range(-w, w + 1):
                x = peak_x + dx
                if 0 <= x < TILE:
                    px[y * TILE + x] = PAPER
    for x in range(TILE):
        for y in (TILE - 3, TILE - 2, TILE - 1):
            px[y * TILE + x] = INK
    return px


PATTERNS = {
    "terrain_tiles/grass": t_grass,
    "terrain_tiles/forest": t_forest,
    "terrain_tiles/water": t_water,
    "terrain_tiles/road": t_road,
    "terrain_tiles/city": t_city,
    "terrain_tiles/rubble": t_rubble,
    "terrain_tiles/mountains": t_mountains,
}


def pack(px):
    if len(px) != TILE * TILE:
        die(f"tile has {len(px)} pixels, want {TILE * TILE}")
    out = bytearray(TILE * TILE // 2)
    for i, v in enumerate(px):
        if v < TRANSPARENT or v > TONE_MAX:
            die(f"pixel value {v} is not 0..{TONE_MAX}")
        out[i >> 1] |= v << ((i & 1) * 4)
    for i, v in enumerate(px):
        if (out[i >> 1] >> ((i & 1) * 4)) & 15 != v:
            die("pack/unpack mismatch")
    return out


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def parse_unit(desc, where):
    """Stats come from the sheet notes, exactly as the old cutter read them."""
    import re
    text = desc.replace("\u2013", "-").replace("\u2014", "-")
    hp = re.search(r"(\d+) HP", text)
    aim = re.search(r"(\d+) aim", text)
    mob = re.search(r"(\d+)-tile", text)
    if not hp or not aim or not mob:
        die(f"{where} is missing HP, aim, or mobility: {desc}")
    if re.search(r"no armour", text):
        armor = 0
    else:
        arm = re.search(r"(\d+) armour", text)
        if not arm:
            die(f"{where} does not say its armour: {desc}")
        armor = int(arm.group(1))
    dmg = re.search(r"(\d+)\s*-\s*(\d+)", text)
    if not dmg:
        die(f"{where} does not say its damage: {desc}")
    dlo, dhi = int(dmg.group(1)), int(dmg.group(2))
    mobility = int(mob.group(1))
    budget = mobility // 3
    if budget < 4:
        budget = 4
    heal_hp, heal_n = 0, 0
    heal = re.search(r"restoring (\d+) HP", text)
    if heal:
        heal_hp = int(heal.group(1))
        heal_n = 2 if "twice" in text else 1
    return {
        "hp": int(hp.group(1)), "aim": int(aim.group(1)), "mv": budget,
        "armor": armor, "dlo": dlo, "dhi": dhi,
        "heal_hp": heal_hp, "heal_n": heal_n,
    }


def unit_flags(sid, desc):
    import re
    flags = 1 if sid == "player_units" else 2
    if re.search(r"flying|aerial|flight|ignoring ground", desc):
        flags |= 4
    if re.search(r"\bmelee\b", desc):
        flags |= 8
    if "stationary" in desc:
        flags |= 16
    if re.search(r"restoring (\d+) HP", desc):
        flags |= 32
    return flags


def load_stats():
    doc = json.loads(STATS.read_text())
    out = {}
    for sec in doc["sections"]:
        for sp in sec["sprites"]:
            key = f"{sec['id']}/{sp['id']}"
            desc = sp.get("description", "")
            if sec["id"] in ("player_units", "enemies"):
                st = parse_unit(desc, key)
                st["flags"] = unit_flags(sec["id"], desc)
            else:
                st = {k: 0 for k in ("hp", "aim", "mv", "armor", "dlo", "dhi", "heal_hp", "heal_n", "flags")}
            out[key] = {"label": sp["label"], "stats": st}
    return out


def main():
    ascii_dump = "--ascii" in sys.argv
    preview = None
    args = [a for a in sys.argv[1:] if a != "--ascii"]
    if args[:1] == ["--preview"] and len(args) == 2:
        preview = Path(args[1])
    elif args:
        die("usage: bake_bold_sprites.py [--ascii] [--preview out.png]")

    stats = load_stats()
    manifest = json.loads(MANIFEST.read_text())

    tiles = {}          # id -> bytearray of TILE*TILE values
    source = {}         # id -> description of where it came from

    for sheet in manifest["sheets"]:
        path = ROOT / sheet["file"]
        if not path.exists():
            die(f"missing sheet {path}")
        cols, rows = sheet["grid"]
        _probe, cw, ch = load_cell(path, cols, rows, 0)
        if len(sheet["sprites"]) > cols * rows:
            die(f"{path} claims {len(sheet['sprites'])} sprites but has {cols * rows} cells")
        if "style" in sheet or "thicken" in sheet:
            die(f"{path.name}: 'style'/'thicken' are gone; every cell bakes to a tone now")
        for i, sid in enumerate(sheet["sprites"]):
            if sid not in stats:
                die(f"{sid} is not in {STATS.name}")
            g, gw, gh = load_cell(path, cols, rows, i)
            tile = cell_to_tile(g, gw, gh)
            if tile is None:
                die(f"{sid} (cell {i} of {path.name}) has no ink")
            # Tone mass: a pixel at the darkest level counts 8, paper counts
            # 0, so for all-black art this is the ink count the guard used to
            # measure, and a grey silhouette now fails for being faint rather
            # than for being thin.
            tone = sum(v - PAPER for v in tile if v > PAPER)
            body = sum(1 for v in tile if v != TRANSPARENT)
            pct = 100 * tone // (BAYER_LEVELS * body) if body else 0
            if pct < 15:
                die(f"{sid} came out only {pct}% ink; too faint to read on e-ink")
            tiles[sid] = tile
            source[sid] = f"{path.name} cell {i} ({tone} tone-px, {pct}% ink)"

    for sid, fn in PATTERNS.items():
        tiles[sid] = fn()
        source[sid] = f"pattern {fn.__name__}"

    missing = [s for s in manifest["required"] if s not in tiles]
    if missing:
        die("manifest requires sprites that were not baked: " + ", ".join(missing))

    order = [s for s in manifest["order"] if s in tiles]
    for sid in tiles:
        if sid not in order:
            die(f"{sid} was baked but is not in the order list")

    idents = []
    for sid in order:
        sid_sec, sid_name = sid.split("/", 1)
        enum = "SP_" + (manifest["prefix"][sid_sec] + "_" + sid_name).upper().replace("/", "_")
        idents.append(enum)

    lines = []
    lines.append("/* Generated by tools/bake_bold_sprites.py. Do not edit. */")
    lines.append("#ifndef XCOM_SPRITES_H")
    lines.append("#define XCOM_SPRITES_H")
    lines.append("#include <string.h>")
    lines.append(f"#define SP_TILE {TILE}")
    lines.append(f"#define SP_N {(TILE * TILE) // 2}")
    lines.append(f"#define SP_COUNT {len(order)}")
    lines.append("enum {")
    for i, name in enumerate(idents):
        lines.append(f"    {name} = {i},")
    lines.append("};")
    lines.append("enum {")
    lines.append("    SPF_PLAYER = 1,")
    lines.append("    SPF_ENEMY = 2,")
    lines.append("    SPF_FLY = 4,")
    lines.append("    SPF_MELEE = 8,")
    lines.append("    SPF_STEADY = 16,")
    lines.append("    SPF_HEAL = 32")
    lines.append("};")
    lines.append("typedef struct {")
    lines.append("    int hp, aim, mv, armor, dlo, dhi;")
    lines.append("    int heal_hp, heal_n, flags;")
    lines.append("} sp_info_t;")
    lines.append("static const char *const sp_id[SP_COUNT] = {")
    for sid in order:
        lines.append(f"    {c_str(sid)},")
    lines.append("};")
    lines.append("static const char *const sp_label[SP_COUNT] = {")
    for sid in order:
        lines.append(f"    {c_str(stats[sid]['label'])},")
    lines.append("};")
    lines.append("static const sp_info_t sp_info[SP_COUNT] = {")
    for sid in order:
        st = stats[sid]["stats"]
        lines.append(
            "    {%d, %d, %d, %d, %d, %d, %d, %d, %d},"
            % (st["hp"], st["aim"], st["mv"], st["armor"], st["dlo"], st["dhi"],
               st["heal_hp"], st["heal_n"], st["flags"]))
    lines.append("};")
    lines.append("static const unsigned char sp_px[SP_COUNT][SP_N] = {")
    for sid in order:
        blob = pack(tiles[sid])
        lines.append("    {" + ",".join(f"0x{b:02x}" for b in blob) + "},")
    lines.append("};")
    lines.append("/* 4 bits per pixel: 0 transparent, 1 paper, 2..9 a tone whose")
    lines.append(" * value minus 1 is the ink level 0..8, which xcom rescales to the")
    lines.append(" * 0..15 the engine's grey layer takes. */")
    lines.append("static int sp_at(int id, int x, int y) {")
    lines.append("    int i = y * SP_TILE + x;")
    lines.append("    return (sp_px[id][i >> 1] >> ((i & 1) * 4)) & 15;")
    lines.append("}")
    lines.append("static int sp_find(const char *id) {")
    lines.append("    for (int i = 0; i < SP_COUNT; i++)")
    lines.append("        if (strcmp(sp_id[i], id) == 0) return i;")
    lines.append("    return -1;")
    lines.append("}")
    lines.append("#endif")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines) + "\n")
    for sid in order:
        print(f"  {sid}: {source[sid]}")
    print(f"wrote {OUT} ({len(order)} sprites)")

    if ascii_dump:
        for sid in order:
            t = tiles[sid]
            print(f"\n== {sid}")
            for y in range(0, TILE, 2):
                row = ""
                for x in range(TILE):
                    v = t[y * TILE + x]
                    row += ASCII[v]
                print(row)

    if preview:
        write_preview(preview, order, tiles)
        print(f"wrote {preview}")


def write_preview(path, order, tiles):
    """Each tile twice: the tone as stored, and the panel's own answer to it.

    Left is data (transparent as light grey so the silhouette edge is
    visible), right is what ek_grey_rect() will actually put on the panel
    once ink15() has scaled the level. If the two disagree, the renderer
    is the thing to suspect.
    """
    cols = 6
    cell_w, cell_h = TILE * 2 + 12, TILE + 8
    rows = (len(order) + cols - 1) // cols
    pw, ph = cols * cell_w, rows * cell_h
    img = Image.new("RGB", (pw, ph), (32, 32, 32))
    px = img.load()
    for i, sid in enumerate(order):
        cx = (i % cols) * cell_w + 4
        cy = (i // cols) * cell_h + 4
        t = tiles[sid]
        for y in range(TILE):
            for x in range(TILE):
                v = t[y * TILE + x]
                if v == TRANSPARENT:
                    px[cx + x, cy + y] = (190, 190, 190)
                    px[cx + TILE + 8 + x, cy + y] = (216, 216, 216)
                    continue
                level = v - PAPER
                tone = 255 - level * 255 // BAYER_LEVELS
                px[cx + x, cy + y] = (tone, tone, tone)
                g = GREY_RAMP[ink15(level)]
                px[cx + TILE + 8 + x, cy + y] = (g, g, g)
    img.save(path)


if __name__ == "__main__":
    main()
