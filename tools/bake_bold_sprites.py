#!/usr/bin/env python3
"""Bake bold sprites for the Kindle panel and write src/sprites.h.

The old cutter (tools/slice_sprites.py) took a painted sheet and *dithered*
it to 2 bits. At 40x40 that turns into grey mush: a soldier and a crate
dither to the same speckle. This tool does the opposite. It takes bold
line art drawn at 512x512 per sprite, thresholds it hard (no dither), then
dilates the ink by one pixel so no stroke is thinner than 2 px on the
panel. Bold in, bold out.

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

TRANSPARENT, PAPER, INK = 0, 1, 2

# Hard threshold. Slightly below mid-grey keeps the model's anti-aliased
# edge pixels as ink, which is what makes the stroke read at 40 px.
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


def erode(ink, w, h, passes):
    """Shrink ink by `passes` pixels (4-neighbour)."""
    cur = ink
    for _ in range(passes):
        out = bytearray(cur)
        for y in range(h):
            for x in range(w):
                if not cur[y * w + x]:
                    continue
                if x == 0 or x + 1 == w or y == 0 or y + 1 == h \
                        or not (cur[y * w + x - 1] and cur[y * w + x + 1]
                                and cur[(y - 1) * w + x] and cur[(y + 1) * w + x]):
                    out[y * w + x] = 0
        cur = out
    return cur


def cell_to_tile(g, cw, ch, thicken, hollow, trace=None):
    """One 512x512 cell of generated art -> TILE*TILE values of 0/1/2.

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
    if sum(ink) * 2 > cw * ch:
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
    small = area_resize(cut, bw, bh, dw, dh)
    note("small", small, dw, dh)
    small_ink = threshold(small, dw * dh)
    if sum(small_ink) == 0:
        die("art vanished when scaled to 40 px")
    note("small_ink", small_ink, dw, dh)
    small_ink = dilate(small_ink, dw, dh, thicken)
    mask = shape_mask(small_ink, dw, dh)
    if hollow:
        # Friendly units read as a bold outline with a white body, hostiles
        # as solid black. At 40 px the interior detail is gone anyway, so the
        # silhouette alone has to carry the distinction -- and black vs white
        # is the only contrast an e-ink panel guarantees. An outline that
        # erodes to nothing is a hard error, not a thin sprite.
        eaten = erode(small_ink, dw, dh, 2)
        if sum(eaten) == 0:
            die("hollow outline disappeared at 40 px; the shape is too thin")
        small_ink = bytearray(1 if a and not b else 0
                              for a, b in zip(small_ink, eaten))
    note("cut", small_ink, dw, dh)

    tile = bytearray(TILE * TILE)
    ox = (TILE - dw) // 2
    oy = (TILE - dh) // 2
    for y in range(dh):
        for x in range(dw):
            i = y * dw + x
            if not mask[i]:
                continue
            tile[(oy + y) * TILE + ox + x] = INK if small_ink[i] else PAPER
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
    out = bytearray(TILE * TILE // 4)
    for i, v in enumerate(px):
        if v not in (0, 1, 2):
            die(f"pixel value {v}")
        out[i >> 2] |= v << ((i & 3) * 2)
    for i, v in enumerate(px):
        if (out[i >> 2] >> ((i & 3) * 2)) & 3 != v:
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
        for i, sid in enumerate(sheet["sprites"]):
            if sid not in stats:
                die(f"{sid} is not in {STATS.name}")
            g, gw, gh = load_cell(path, cols, rows, i)
            tile = cell_to_tile(g, gw, gh, sheet.get("thicken", 1),
                                sheet.get("style", "solid") == "hollow")
            if tile is None:
                die(f"{sid} (cell {i} of {path.name}) has no ink")
            ink = sum(1 for v in tile if v == INK)
            body = sum(1 for v in tile if v != TRANSPARENT)
            pct = 100 * ink // body if body else 0
            if pct < 15:
                die(f"{sid} came out only {pct}% ink; too thin to read on e-ink")
            tiles[sid] = tile
            source[sid] = f"{path.name} cell {i} ({ink} ink px, {pct}% of body)"

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
    lines.append(f"#define SP_N {(TILE * TILE) // 4}")
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
    lines.append("static int sp_at(int id, int x, int y) {")
    lines.append("    int i = y * SP_TILE + x;")
    lines.append("    return (sp_px[id][i >> 2] >> ((i & 3) * 2)) & 3;")
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
                    row += "#" if v == INK else ("." if v == PAPER else " ")
                print(row)

    if preview:
        write_preview(preview, order, tiles)
        print(f"wrote {preview}")


def write_preview(path, order, tiles):
    """Each tile twice: the 2-bit data, and how it looks on the panel
    (transparent shown as light grey so the silhouette edge is visible)."""
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
                if v == INK:
                    c = (0, 0, 0)
                elif v == PAPER:
                    c = (255, 255, 255)
                else:
                    c = (190, 190, 190)
                px[cx + x, cy + y] = c
                if v == INK:
                    c2 = (0, 0, 0)
                elif v == PAPER:
                    c2 = (255, 255, 255)
                else:
                    c2 = (216, 216, 216)
                px[cx + TILE + 8 + x, cy + y] = c2
    img.save(path)


if __name__ == "__main__":
    main()
